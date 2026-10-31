// Assembly-only inputs for recomp-kit. All game-derived output stays ignored.
// @category BlurResearch
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.nio.file.*;

public class ExportRecompListings extends GhidraScript {
    public void run() throws Exception {
        String hash = "dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f";
        if (!hash.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Recompilation requires the documented Blur.exe baseline");
        Path out = Path.of(getScriptArgs()[0], currentProgram.getName());
        Files.createDirectories(out.resolve("functions"));
        int count = 0;
        try (PrintWriter index = new PrintWriter(Files.newBufferedWriter(out.resolve("functions.tsv")))) {
            index.println("address\tname\tbytes\tstatus\tmessage");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                monitor.checkCancelled();
                Function f = functions.next();
                String address = f.getEntryPoint().toString();
                try (PrintWriter asm = new PrintWriter(Files.newBufferedWriter(out.resolve("functions/" + address + ".asm")))) {
                    for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true))
                        asm.printf("%s  %s%n", ins.getAddress(), ins.toString());
                }
                index.printf("%s\t%s\t%d\tassembly\tAutomatic function boundaries; unverified except documented slices%n",
                    address, f.getName(), f.getBody().getNumAddresses());
                count++;
            }
        }
        Files.writeString(out.resolve("summary.txt"), "sha256=" + hash + "\ndiscovered=" + count +
            "\nAssembly export, not recovered source or proof of complete code coverage.\n");
        println("Exported " + count + " assembly listings");
    }
}
