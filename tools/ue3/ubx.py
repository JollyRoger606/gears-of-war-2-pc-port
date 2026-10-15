"""python -I tools/ue3/ubx.py <pkg.upk> <export index> [...]: lists bytecode of exports by index."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upk import Package
from ubc import dump_export
pk = Package(sys.argv[1])
for a in sys.argv[2:]:
    dump_export(pk, int(a))
    print()
