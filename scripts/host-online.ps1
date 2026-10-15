# Opens the System Link ports on every UPnP router between this PC and the internet, with a lease,
# and closes them again. Usage: docs/ONLINE.md, "Host setup". Runs in Windows PowerShell 5.1 and pwsh.
#   powershell -ExecutionPolicy Bypass -File scripts\host-online.ps1 -Start [-LeaseHours 6] [-Hostname suruworks.com] [-AllowPeers a,b]
#   powershell -ExecutionPolicy Bypass -File scripts\host-online.ps1 -Status
#   powershell -ExecutionPolicy Bypass -File scripts\host-online.ps1 -Stop
param(
    [switch]$Start,
    [switch]$Stop,
    [switch]$Status,
    [ValidateRange(0.1, 168)][double]$LeaseHours = 6,
    [string]$Hostname = 'suruworks.com',
    [string[]]$AllowPeers = @(),
    [ValidateRange(1024, 64534)][int]$PortOffset = 40000
)

$ErrorActionPreference = 'Stop'

$MappingDescription = 'gow2 System Link'
$BeaconPort = 14001
$GamePortBase = 1000
$DefaultPortOffset = 40000
$SsdpGroup = '239.255.255.250'
$SsdpPort = 1900
$SsdpWaitMs = 2500
$TraceTarget = '1.1.1.1'
$PublicAddressService = 'https://api.ipify.org'
$MaxRouters = 4
$LeaseProbeDelayMs = 2500
$IgdSearchTargets = @(
    'urn:schemas-upnp-org:device:InternetGatewayDevice:2',
    'urn:schemas-upnp-org:device:InternetGatewayDevice:1'
)
$WanServiceTypes = @(
    'urn:schemas-upnp-org:service:WANIPConnection:2',
    'urn:schemas-upnp-org:service:WANIPConnection:1',
    'urn:schemas-upnp-org:service:WANPPPConnection:1'
)
$UpnpNotAuthorized = 606
$UpnpNoSuchEntry = 714
$UpnpConflictInMapping = 718
$UpnpOnlyPermanentLeases = 725
$AndroidConfigDir = '/sdcard/Android/data/com.santiquiroz.gow2recomp/files/'

function Write-Section([string]$Title) { Write-Host ''; Write-Host "== $Title" -ForegroundColor Cyan }
function Write-Note([string]$Text) { Write-Host "  $Text" }
function Write-Warn([string]$Text) { Write-Host "  WARNING: $Text" -ForegroundColor Yellow }

function Select-Mode([bool]$IsStart, [bool]$IsStop, [bool]$IsStatus) {
    $chosen = @($IsStart, $IsStop, $IsStatus | Where-Object { $_ }).Count
    if ($chosen -gt 1) { throw 'Choose one of -Start, -Stop or -Status.' }
    if ($IsStart) { return 'Start' }
    if ($IsStop) { return 'Stop' }
    'Status'
}

function Get-SystemLinkPorts([int]$Offset) { @($BeaconPort, ($GamePortBase + $Offset)) }

function Get-IPv4Bytes([string]$Address) { [Net.IPAddress]::Parse($Address).GetAddressBytes() }

function Test-PrivateIPv4([string]$Address) {
    $b = Get-IPv4Bytes $Address
    ($b[0] -eq 10) -or ($b[0] -eq 172 -and $b[1] -ge 16 -and $b[1] -le 31) -or ($b[0] -eq 192 -and $b[1] -eq 168)
}

function Test-SharedIPv4([string]$Address) {
    $b = Get-IPv4Bytes $Address
    $b[0] -eq 100 -and $b[1] -ge 64 -and $b[1] -le 127
}

function Test-PublicIPv4([string]$Address) { -not (Test-PrivateIPv4 $Address) -and -not (Test-SharedIPv4 $Address) }

function New-LanHttpClient {
    Add-Type -AssemblyName System.Net.Http
    $handler = [Net.Http.HttpClientHandler]::new()
    $handler.UseProxy = $false
    $client = [Net.Http.HttpClient]::new($handler)
    $client.Timeout = [TimeSpan]::FromSeconds(6)
    $client.DefaultRequestHeaders.ExpectContinue = $false
    $client
}

function Get-LocalIPv4 {
    $socket = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetwork, [Net.Sockets.SocketType]::Dgram, [Net.Sockets.ProtocolType]::Udp)
    try {
        $socket.Connect($TraceTarget, 53)  # a UDP connect sends nothing; it only picks the default route
        $socket.LocalEndPoint.Address.ToString()
    } finally { $socket.Dispose() }
}

function Get-HopAddress([int]$Hop) {
    $ping = [Net.NetworkInformation.Ping]::new()
    $options = [Net.NetworkInformation.PingOptions]::new($Hop, $true)
    $payload = New-Object byte[] 32
    try {
        foreach ($attempt in 1..3) {
            $reply = $ping.Send($TraceTarget, 1000, $payload, $options)
            if ($reply.Status -eq 'TtlExpired') { return $reply.Address.ToString() }
        }
    } finally { $ping.Dispose() }
    $null
}

function New-SsdpRequest([string]$HostHeader, [string]$SearchTarget) {
    $lines = @('M-SEARCH * HTTP/1.1', "HOST: $HostHeader", 'MAN: "ssdp:discover"', 'MX: 2', "ST: $SearchTarget", '', '')
    [Text.Encoding]::ASCII.GetBytes($lines -join "`r`n")
}

function New-SsdpClient([string]$LocalAddress) {
    $local = [Net.IPAddress]::Parse($LocalAddress)
    $client = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new($local, 0))
    $client.Client.SetSocketOption([Net.Sockets.SocketOptionLevel]::IP, [Net.Sockets.SocketOptionName]::MulticastInterface, $local.GetAddressBytes())
    $client.Client.SetSocketOption([Net.Sockets.SocketOptionLevel]::IP, [Net.Sockets.SocketOptionName]::MulticastTimeToLive, 2)
    $client
}

function Send-IgdSearch($Client, [string]$Target) {
    $endpoint = [Net.IPEndPoint]::new([Net.IPAddress]::Parse($Target), $SsdpPort)
    foreach ($searchTarget in $IgdSearchTargets) {
        $bytes = New-SsdpRequest "${Target}:$SsdpPort" $searchTarget
        [void]$Client.Send($bytes, $bytes.Length, $endpoint)
    }
}

function ConvertFrom-SsdpReply([string]$Text, [string]$Address) {
    $headers = @{}
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^([A-Za-z-]+):\s*(.*)$') { $headers[$Matches[1].ToUpperInvariant()] = $Matches[2].Trim() }
    }
    if (-not $headers['LOCATION']) { return $null }
    [pscustomobject]@{ Address = $Address; Location = $headers['LOCATION']; Server = $headers['SERVER']; SearchTarget = $headers['ST'] }
}

function Receive-SsdpReply($Client) {
    $remote = [Net.IPEndPoint]::new([Net.IPAddress]::Any, 0)
    try { $bytes = $Client.Receive([ref]$remote) }
    catch { return $null }  # an ICMP port-unreachable from a host without SSDP surfaces here
    ConvertFrom-SsdpReply ([Text.Encoding]::ASCII.GetString($bytes)) $remote.Address.ToString()
}

function Receive-SsdpReplies($Client, [int]$WaitMs) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($WaitMs)
    while ($true) {
        $left = ($deadline - [DateTime]::UtcNow).TotalMilliseconds
        if ($left -le 0) { break }
        if (-not $Client.Client.Poll([int]($left * 1000), [Net.Sockets.SelectMode]::SelectRead)) { break }
        $reply = Receive-SsdpReply $Client
        if ($reply) { $reply }
    }
}

function Search-Igds([string]$LocalAddress, [string]$Target) {
    $client = New-SsdpClient $LocalAddress
    try {
        Send-IgdSearch $client $Target
        @(Receive-SsdpReplies $client $SsdpWaitMs)
    } finally { $client.Dispose() }
}

function Read-ContentText($Response) {
    # MiniUPnPd sends charset="utf-8" with quotes, which ReadAsStringAsync rejects
    [Text.Encoding]::UTF8.GetString($Response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult())
}

function Get-HttpText([string]$Url) {
    $response = $script:Http.GetAsync($Url).GetAwaiter().GetResult()
    if (-not $response.IsSuccessStatusCode) { throw "GET $Url answered HTTP $([int]$response.StatusCode)." }
    Read-ContentText $response
}

function Get-DescriptionBase($Description, [string]$Location) {
    $node = $Description.SelectSingleNode("//*[local-name()='URLBase']")
    if ($node -and $node.InnerText.Trim()) { return $node.InnerText.Trim() }
    $Location
}

function Find-WanService([string]$Location) {
    $description = [xml](Get-HttpText $Location)
    $base = [Uri](Get-DescriptionBase $description $Location)
    foreach ($type in $WanServiceTypes) {
        $node = $description.SelectSingleNode("//*[local-name()='service'][normalize-space(*[local-name()='serviceType'])='$type']")
        if (-not $node) { continue }
        $control = $node.SelectSingleNode("*[local-name()='controlURL']").InnerText.Trim()
        return [pscustomobject]@{ ServiceType = $type; ControlUrl = [Uri]::new($base, $control).AbsoluteUri }
    }
    $null
}

function Find-WanServiceOrWarn([string]$Location) {
    try { Find-WanService $Location }
    catch { Write-Warn "Could not read the UPnP description at ${Location}: $($_.Exception.GetBaseException().Message)"; $null }
}

function New-Igd([string]$Address, $Reply, $Service) {
    [pscustomobject]@{
        Address = $Address
        Server = $Reply.Server
        Location = $Reply.Location
        ServiceType = $Service.ServiceType
        ControlUrl = $Service.ControlUrl
    }
}

function Find-RouterIgd([string]$Address, [string]$LocalAddress, $LanReplies) {
    $replies = @($LanReplies | Where-Object { $_.Address -eq $Address })
    if (-not $replies) { $replies = @(Search-Igds $LocalAddress $Address | Where-Object { $_.Address -eq $Address }) }
    $byVersion = @($replies | Sort-Object { $_.SearchTarget -notlike '*:2' })
    foreach ($location in @($byVersion | ForEach-Object { $_.Location } | Select-Object -Unique)) {
        $service = Find-WanServiceOrWarn $location
        $reply = $byVersion | Where-Object { $_.Location -eq $location } | Select-Object -First 1
        if ($service) { return New-Igd $Address $reply $service }
    }
    $null
}

function New-SoapBody([string]$ServiceType, [string]$Action, $Arguments) {
    $fields = foreach ($name in $Arguments.Keys) { '<{0}>{1}</{0}>' -f $name, [Security.SecurityElement]::Escape([string]$Arguments[$name]) }
    '<?xml version="1.0"?>' +
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    ('<s:Body><u:{0} xmlns:u="{1}">{2}</u:{0}></s:Body></s:Envelope>' -f $Action, $ServiceType, (@($fields) -join ''))
}

function Send-SoapRequest($Igd, [string]$Action, [string]$Body) {
    $request = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::Post, $Igd.ControlUrl)
    [void]$request.Headers.TryAddWithoutValidation('SOAPAction', ('"{0}#{1}"' -f $Igd.ServiceType, $Action))
    $request.Content = [Net.Http.StringContent]::new($Body, [Text.Encoding]::UTF8, 'text/xml')
    try {
        $response = $script:Http.SendAsync($request).GetAwaiter().GetResult()
        [pscustomobject]@{ Status = [int]$response.StatusCode; Text = (Read-ContentText $response) }
    } catch {
        throw "The UPnP service of $($Igd.Address) did not answer $Action ($($Igd.ControlUrl)): $($_.Exception.GetBaseException().Message)"
    }
}

function ConvertTo-XmlOrNull([string]$Text) {
    try { [xml]$Text } catch { $null }
}

function New-UpnpResult([int]$Code, [string]$Description, $Values) {
    [pscustomobject]@{ Code = $Code; Description = $Description; Values = $Values }
}

function Get-ChildText($Node, [string]$Name) {
    $child = $Node.SelectSingleNode(".//*[local-name()='$Name']")
    if ($child) { return $child.InnerText.Trim() }
    ''
}

function ConvertFrom-SoapResponse($Response) {
    $document = ConvertTo-XmlOrNull $Response.Text
    if (-not $document) { return New-UpnpResult $Response.Status "HTTP $($Response.Status) without a SOAP body" @{} }
    $fault = $document.SelectSingleNode("//*[local-name()='UPnPError']")
    if ($fault) { return New-UpnpResult ([int](Get-ChildText $fault 'errorCode')) (Get-ChildText $fault 'errorDescription') @{} }
    if ($Response.Status -ge 300) { return New-UpnpResult $Response.Status "HTTP $($Response.Status)" @{} }
    $values = @{}
    $reply = $document.SelectSingleNode("//*[local-name()='Body']/*")
    if ($reply) { foreach ($node in $reply.SelectNodes('*')) { $values[$node.LocalName] = $node.InnerText } }
    New-UpnpResult 0 '' $values
}

function Invoke-UpnpAction($Igd, [string]$Action, $Arguments) {
    $response = Send-SoapRequest $Igd $Action (New-SoapBody $Igd.ServiceType $Action $Arguments)
    ConvertFrom-SoapResponse $response
}

function Assert-UpnpSuccess($Result, [string]$What) {
    if ($Result.Code -eq 0) { return }
    if ($Result.Code -eq $UpnpNotAuthorized) {
        throw "$What was refused (UPnP error 606, Action not authorized): the router answers UPnP but does not let this PC change it. Allow UPnP port mapping in its settings."
    }
    throw "$What failed: UPnP error $($Result.Code) $($Result.Description)."
}

function Get-ExternalAddress($Igd) {
    $result = Invoke-UpnpAction $Igd 'GetExternalIPAddress' ([ordered]@{})
    Assert-UpnpSuccess $result "GetExternalIPAddress on $($Igd.Address)"
    $address = [string]$result.Values['NewExternalIPAddress']
    if (-not $address -or $address -eq '0.0.0.0') { throw "The router at $($Igd.Address) reports no WAN address: is it connected to the internet?" }
    $address
}

function New-Router([int]$Hop, $Igd, [string]$InternalClient, [string]$ExternalAddress) {
    [pscustomobject]@{ Hop = $Hop; Address = $Igd.Address; Igd = $Igd; InternalClient = $InternalClient; ExternalAddress = $ExternalAddress }
}

function New-RouterPath($Routers, [string]$End, [string]$EndAddress, [int]$EndHop) {
    # PowerShell 5.1 throws 'argument types do not match' on @() of an empty generic List here
    [pscustomobject]@{ Routers = $Routers.ToArray(); End = $End; EndAddress = $EndAddress; EndHop = $EndHop }
}

function Get-PathEnd([string]$ExternalAddress) {
    if (Test-SharedIPv4 $ExternalAddress) { return 'shared' }
    if (Test-PrivateIPv4 $ExternalAddress) { return $null }
    'public'
}

function Get-RouterPath([string]$LocalAddress) {
    $routers = New-Object Collections.Generic.List[object]
    if (Test-PublicIPv4 $LocalAddress) { return New-RouterPath $routers 'direct' $LocalAddress 0 }
    $lanReplies = @(Search-Igds $LocalAddress $SsdpGroup)
    $client = $LocalAddress
    for ($hop = 1; $hop -le $MaxRouters; $hop++) {
        $address = Get-HopAddress $hop
        if (-not $address) { return New-RouterPath $routers 'no-hop' $null $hop }
        $igd = Find-RouterIgd $address $LocalAddress $lanReplies
        if (-not $igd) { return New-RouterPath $routers 'no-igd' $address $hop }
        $external = Get-ExternalAddress $igd
        $routers.Add((New-Router $hop $igd $client $external))
        $end = Get-PathEnd $external
        if ($end) { return New-RouterPath $routers $end $external $hop }
        $client = $external
    }
    New-RouterPath $routers 'too-deep' $null $MaxRouters
}

function Get-ServiceName([string]$ServiceType) { ($ServiceType -split ':')[-2..-1] -join ':' }

function Show-Router($Router) {
    $igd = $Router.Igd
    Write-Note ("Router {0}: {1} ({2}; {3}), WAN address {4}" -f $Router.Hop, $Router.Address, $igd.Server, (Get-ServiceName $igd.ServiceType), $Router.ExternalAddress)
    Write-Note ("  its mappings point to {0}; control URL {1}" -f $Router.InternalClient, $igd.ControlUrl)
}

function Get-NoIgdMessage($Path) {
    $address = $Path.EndAddress
    if ($Path.EndHop -eq 1) {
        return "Your router $address answers no UPnP search: UPnP (IGD) is off or blocked. Turn it on in the router's settings, or forward UDP $($script:Ports -join ' and ') by hand (docs/ONLINE.md)."
    }
    $inner = $Path.Routers[-1]
    if (Test-SharedIPv4 $address) { return Get-CgnatMessage "the next hop $address is in 100.64.0.0/10" }
    "The router at $address (hop $($Path.EndHop)) answers no UPnP search. The router before it has the private WAN address $($inner.ExternalAddress), so $address is either a second router of yours with UPnP off (turn UPnP on, or forward UDP $($script:Ports -join ' and ') to $($inner.ExternalAddress) by hand) or the ISP's carrier-grade NAT (then no forward can work)."
}

function Get-CgnatMessage([string]$Evidence) {
    "Carrier-grade NAT detected ($Evidence): the ISP shares one public address between customers, so no port mapping can reach this PC. Ask the ISP for a public IPv4 address, or use Tailscale or ZeroTier (docs/ONLINE.md)."
}

function Get-PathEndMessage($Path) {
    switch ($Path.End) {
        'public' { return $null }
        'direct' { return $null }
        'shared' { return Get-CgnatMessage "the outermost router's WAN address $($Path.EndAddress) is in 100.64.0.0/10" }
        'no-igd' { return Get-NoIgdMessage $Path }
        'no-hop' { return "Hop $($Path.EndHop) toward the internet did not answer a traceroute probe; the router path could not be followed." }
        default { return "More than $MaxRouters routers in a row have private WAN addresses; this script stops there." }
    }
}

function Show-Path($Path) {
    Write-Section 'Routers between this PC and the internet'
    if ($Path.End -eq 'direct') { Write-Note "This PC has the public address $($Path.EndAddress): no router to map." }
    foreach ($router in $Path.Routers) { Show-Router $router }
    $message = Get-PathEndMessage $Path
    if ($message) { Write-Warn $message }
}

function Get-InternetIPv4 {
    try { ([string](Invoke-RestMethod -Uri $PublicAddressService -TimeoutSec 10 -UseBasicParsing)).Trim() }
    catch { $null }
}

function Get-OutermostAddress($Path) {
    if ($Path.End -eq 'direct') { return $Path.EndAddress }
    $Path.Routers[-1].ExternalAddress
}

function Assert-ReachableFromInternet($Path) {
    $message = Get-PathEndMessage $Path
    if ($message) { throw $message }
    $routerView = Get-OutermostAddress $Path
    $internetView = Get-InternetIPv4
    Write-Section 'Public address'
    Write-Note "Outermost router's WAN address: $routerView"
    if (-not $internetView) { Write-Warn "$PublicAddressService did not answer; the address was not cross-checked."; return $routerView }
    Write-Note "$PublicAddressService sees this PC as: $internetView"
    if ($internetView -ne $routerView) {
        throw (Get-CgnatMessage "the outermost router reports $routerView but the internet sees $internetView; a VPN or proxy on this PC gives the same symptom")
    }
    $routerView
}

function Get-PortMapping($Router, [int]$Port) {
    $arguments = [ordered]@{ NewRemoteHost = ''; NewExternalPort = $Port; NewProtocol = 'UDP' }
    $result = Invoke-UpnpAction $Router.Igd 'GetSpecificPortMappingEntry' $arguments
    if ($result.Code -eq $UpnpNoSuchEntry) { return $null }
    Assert-UpnpSuccess $result "Reading the UDP $Port mapping on $($Router.Address)"
    [pscustomobject]@{
        Port = $Port
        InternalClient = [string]$result.Values['NewInternalClient']
        InternalPort = [int]$result.Values['NewInternalPort']
        Enabled = [string]$result.Values['NewEnabled']
        Description = [string]$result.Values['NewPortMappingDescription']
        LeaseSeconds = [int]$result.Values['NewLeaseDuration']
    }
}

function Get-MappingVerdict($Router, $Mapping, [int]$Port) {
    if (-not $Mapping) { return 'missing' }
    if ($Mapping.InternalClient -ne $Router.InternalClient -or $Mapping.InternalPort -ne $Port) { return 'conflict' }
    if ($Mapping.Description -eq $MappingDescription) { return 'ours' }
    'foreign'
}

function Format-Lease([int]$Seconds) {
    if ($Seconds -le 0) { return 'no lease (stays until deleted or the router restarts)' }
    $span = [TimeSpan]::FromSeconds($Seconds)
    '{0}h{1:00}m left' -f [int][Math]::Floor($span.TotalHours), $span.Minutes
}

function Format-Mapping($Router, $Mapping) {
    "UDP {0} on {1} -> {2}:{3} '{4}', enabled {5}, {6}" -f $Mapping.Port, $Router.Address, $Mapping.InternalClient, $Mapping.InternalPort, $Mapping.Description, $Mapping.Enabled, (Format-Lease $Mapping.LeaseSeconds)
}

function Get-MappingPlan($Path, $Ports) {
    foreach ($router in $Path.Routers) {
        foreach ($port in $Ports) {
            $mapping = Get-PortMapping $router $port
            [pscustomobject]@{ Router = $router; Port = $port; Mapping = $mapping; Verdict = (Get-MappingVerdict $router $mapping $port) }
        }
    }
}

function Assert-NoConflicts($Plan) {
    $conflicts = @($Plan | Where-Object { $_.Verdict -eq 'conflict' })
    if (-not $conflicts) { return }
    Write-Section 'Ports already taken'
    foreach ($item in $conflicts) { Write-Warn ("taken " + (Format-Mapping $item.Router $item.Mapping)) }
    throw "Refusing to map: the ports above already go to another device or port. Free them on the router (or stop the program that made them), then run -Start again."
}

function Request-Mapping($Router, [int]$Port, [int]$LeaseSeconds) {
    $arguments = [ordered]@{
        NewRemoteHost = ''; NewExternalPort = $Port; NewProtocol = 'UDP'; NewInternalPort = $Port
        NewInternalClient = $Router.InternalClient; NewEnabled = 1; NewPortMappingDescription = $MappingDescription
        NewLeaseDuration = $LeaseSeconds
    }
    $result = Invoke-UpnpAction $Router.Igd 'AddPortMapping' $arguments
    if ($result.Code -ne $UpnpOnlyPermanentLeases) { return $result }
    Write-Warn "$($Router.Address) accepts only permanent mappings: UDP $Port stays open until -Stop."
    $arguments['NewLeaseDuration'] = 0
    Invoke-UpnpAction $Router.Igd 'AddPortMapping' $arguments
}

function Add-MappingWithLease($Router, [int]$Port, [int]$LeaseSeconds, [bool]$IsOurs) {
    $result = Request-Mapping $Router $Port $LeaseSeconds
    if ($result.Code -eq $UpnpConflictInMapping -and $IsOurs) {
        # MiniUPnPd 1.x answers 718 even when the same client adds its own mapping again
        Remove-PortMappingEntry $Router $Port
        $result = Request-Mapping $Router $Port $LeaseSeconds
    }
    if ($result.Code -eq $UpnpConflictInMapping) { throw "$($Router.Address) refused UDP ${Port}: it is already mapped to another device (UPnP error 718)." }
    Assert-UpnpSuccess $result "Mapping UDP $Port on $($Router.Address) to $($Router.InternalClient)"
}

function Confirm-Mapping($Router, [int]$Port, [string]$Label) {
    $mapping = Get-PortMapping $Router $Port
    if ((Get-MappingVerdict $Router $mapping $Port) -ne 'ours') {
        throw "$($Router.Address) accepted UDP $Port but does not list it for $($Router.InternalClient) afterwards."
    }
    Write-Note ($Label.PadRight(9) + (Format-Mapping $Router $mapping))
    if ($mapping.LeaseSeconds -le 0) { Write-Warn "$($Router.Address) keeps UDP $Port without a lease: run -Stop when the session ends." }
}

function Remove-PortMappingEntry($Router, [int]$Port) {
    $arguments = [ordered]@{ NewRemoteHost = ''; NewExternalPort = $Port; NewProtocol = 'UDP' }
    $result = Invoke-UpnpAction $Router.Igd 'DeletePortMapping' $arguments
    Assert-UpnpSuccess $result "Deleting UDP $Port on $($Router.Address)"
}

function Remove-PortMapping($Router, [int]$Port) {
    Remove-PortMappingEntry $Router $Port
    if (Get-PortMapping $Router $Port) { throw "$($Router.Address) accepted the delete of UDP $Port but still lists it." }
    Write-Note "deleted  UDP $Port on $($Router.Address) (was -> $($Router.InternalClient):$Port)"
}

function Undo-Mappings($Created) {
    foreach ($item in $Created) {
        try { Remove-PortMapping $item.Router $item.Port }
        catch { Write-Warn "Could not undo UDP $($item.Port) on $($item.Router.Address): $($_.Exception.Message)" }
    }
}

function Add-PlannedMappings($Plan, [int]$LeaseSeconds) {
    $created = New-Object Collections.Generic.List[object]
    try {
        foreach ($item in $Plan) {
            if ($item.Verdict -eq 'foreign') { Write-Note ("kept     " + (Format-Mapping $item.Router $item.Mapping) + " (made by another program; -Stop leaves it)"); continue }
            $isOurs = $item.Verdict -eq 'ours'
            Add-MappingWithLease $item.Router $item.Port $LeaseSeconds $isOurs
            if (-not $isOurs) { $created.Add($item) }
            Confirm-Mapping $item.Router $item.Port $(if ($isOurs) { 'renewed' } else { 'mapped' })
        }
    } catch {
        Write-Warn 'Undoing the mappings this run created.'
        Undo-Mappings $created
        throw
    }
}

function Test-LeaseCountsDown($Item, [int]$LeaseSeconds) {
    $mapping = Get-PortMapping $Item.Router $Item.Port
    (-not $mapping) -or ($mapping.LeaseSeconds -lt $LeaseSeconds)
}

function Show-StaticLeaseWarnings($Plan, [int]$LeaseSeconds) {
    $probes = @($Plan | Where-Object { $_.Verdict -ne 'foreign' } | Group-Object { $_.Router.Address } | ForEach-Object { $_.Group[0] })
    if (-not $probes) { return }
    Start-Sleep -Milliseconds $LeaseProbeDelayMs  # a lease that counts down has dropped by then
    foreach ($item in $probes) {
        if (Test-LeaseCountsDown $item $LeaseSeconds) { continue }
        Write-Warn "$($item.Router.Address) does not count its lease down (still $LeaseSeconds s after $($LeaseProbeDelayMs / 1000) s). Routers like that can keep the mappings after the lease ends: run -Stop when the session ends."
    }
}

function Resolve-IPv4Addresses([string]$Name) {
    try { @([Net.Dns]::GetHostAddresses($Name) | Where-Object { $_.AddressFamily -eq 'InterNetwork' } | ForEach-Object { $_.ToString() }) }
    catch { @() }
}

function Select-PeerName([string]$Name, [string]$PublicAddress) {
    Write-Section 'DNS name'
    if (-not $Name) { Write-Note "No -Hostname: friends use the address $PublicAddress."; return $PublicAddress }
    $resolved = @(Resolve-IPv4Addresses $Name)
    if ($resolved -contains $PublicAddress) { Write-Note "$Name resolves to $PublicAddress."; return $Name }
    $seen = if ($resolved) { $resolved -join ', ' } else { 'nothing' }
    Write-Warn "$Name resolves to $seen, not $PublicAddress, so friends would not reach this PC through it. Point its A record at $PublicAddress or pass -Hostname; the settings below use the address."
    $PublicAddress
}

function Get-AllowPeersText([string[]]$Peers) {
    $list = @($Peers | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($list) { return $list -join ',' }
    '<friend-address-or-name>,<another>'
}

function Get-OffsetArgument {
    if ($PortOffset -eq $DefaultPortOffset) { return '' }
    " --net_port_offset=$PortOffset"
}

function Show-GameSettings([string]$PeerName, [int]$LeaseSeconds) {
    $offset = Get-OffsetArgument
    Write-Section 'Game settings'
    Write-Note 'Host (this PC); net_allow_peers lists your friends'' public addresses or DNS names:'
    Write-Note "  gow2.exe --net_link_active=true --net_public_address=$PeerName --net_allow_peers=$(Get-AllowPeersText $AllowPeers) --user_name=<name>$offset"
    Write-Note 'Friends on PC:'
    Write-Note "  gow2.exe --net_link_active=true --net_peers=$PeerName --user_name=<name>$offset"
    Write-Note "Friends on Android, in ${AndroidConfigDir}gow2.toml:"
    Write-Note '  net_link_active = true'
    Write-Note "  net_peers = `"$PeerName`""
    Write-Note '  user_name = "<name>"'
    if ($offset) { Write-Note "  net_port_offset = $PortOffset" }
    Write-Note "Windows Firewall must allow the host's gow2.exe in on UDP $($script:Ports -join ' and ') (docs/ONLINE.md)."
    Write-Note ("The mappings expire at {0:yyyy-MM-dd HH:mm}; run -Stop when the session ends." -f (Get-Date).AddSeconds($LeaseSeconds))
}

function Get-DiscoveredPath {
    $local = Get-LocalIPv4
    Write-Section 'This PC'
    Write-Note "LAN address $local (default route), System Link ports UDP $($script:Ports -join ' and ')"
    $path = Get-RouterPath $local
    Show-Path $path
    $path
}

function Invoke-Start {
    $path = Get-DiscoveredPath
    $public = Assert-ReachableFromInternet $path
    $leaseSeconds = [int][Math]::Round($LeaseHours * 3600)
    $plan = @(Get-MappingPlan $path $script:Ports)
    Assert-NoConflicts $plan
    Write-Section "Mappings (lease $LeaseHours h)"
    Add-PlannedMappings $plan $leaseSeconds
    Show-StaticLeaseWarnings $plan $leaseSeconds
    Show-GameSettings (Select-PeerName $Hostname $public) $leaseSeconds
}

function Remove-OwnMapping($Item) {
    if ($Item.Verdict -eq 'ours') { Remove-PortMapping $Item.Router $Item.Port; return }
    if ($Item.Verdict -eq 'missing') { Write-Note "none     UDP $($Item.Port) on $($Item.Router.Address)"; return }
    Write-Note ("left     " + (Format-Mapping $Item.Router $Item.Mapping) + " (not made by this script)")
}

function Invoke-Stop {
    $path = Get-DiscoveredPath
    Write-Section 'Deleting the mappings this script made'
    foreach ($item in @(Get-MappingPlan $path $script:Ports)) { Remove-OwnMapping $item }
}

function Show-MappingStatus($Item) {
    if ($Item.Verdict -eq 'missing') { Write-Note "none     UDP $($Item.Port) on $($Item.Router.Address)"; return }
    $owner = if ($Item.Verdict -eq 'ours') { '' } else { " ($($Item.Verdict): not made by this script)" }
    Write-Note ("present  " + (Format-Mapping $Item.Router $Item.Mapping) + $owner)
}

function Invoke-Status {
    $path = Get-DiscoveredPath
    Write-Section 'System Link mappings'
    foreach ($item in @(Get-MappingPlan $path $script:Ports)) { Show-MappingStatus $item }
}

try {
    $mode = Select-Mode $Start.IsPresent $Stop.IsPresent $Status.IsPresent
    $script:Ports = Get-SystemLinkPorts $PortOffset
    $script:Http = New-LanHttpClient
    Write-Host ("gow2 host-online -{0}, {1:yyyy-MM-dd HH:mm:ss}" -f $mode, (Get-Date))
    switch ($mode) {
        'Start' { Invoke-Start }
        'Stop' { Invoke-Stop }
        default { Invoke-Status }
    }
    exit 0
} catch {
    Write-Host ''
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    if ($_.FullyQualifiedErrorId -ne $_.Exception.Message) { Write-Host $_.ScriptStackTrace }  # not one of this script's own messages
    exit 1
}
