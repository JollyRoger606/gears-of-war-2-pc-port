// Dumps the loaded (decrypted, decompressed) image for offline scanning.
// Usage (headless postScript): ExportImage.java <out_dir>
// Writes <out_dir>/blocks.tsv (name, start, size, r, w, x) and <out_dir>/<start>.bin per
// initialized block, plus functions.tsv (entry, body min, body max) when analysis ran.
//@category gow2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;

public class ExportImage extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "image_dump");
        out.mkdirs();
        try (PrintWriter tsv = new PrintWriter(new File(out, "blocks.tsv"))) {
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                tsv.printf("%s\t%08X\t%d\t%b\t%b\t%b%n", b.getName(), b.getStart().getOffset(),
                        b.getSize(), b.isRead(), b.isWrite(), b.isExecute());
                if (!b.isInitialized()) continue;
                byte[] bytes = new byte[(int) b.getSize()];
                b.getBytes(b.getStart(), bytes);
                try (FileOutputStream f = new FileOutputStream(
                        new File(out, String.format("%08X.bin", b.getStart().getOffset())))) {
                    f.write(bytes);
                }
            }
        }
        try (PrintWriter tsv = new PrintWriter(new File(out, "functions.tsv"))) {
            for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
                tsv.printf("%08X\t%08X\t%08X%n", fn.getEntryPoint().getOffset(),
                        fn.getBody().getMinAddress().getOffset(), fn.getBody().getMaxAddress().getOffset());
            }
        }
        println("ExportImage: wrote " + out.getAbsolutePath());
    }
}
