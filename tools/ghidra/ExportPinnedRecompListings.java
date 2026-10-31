// Hash-verified assembly-only export for an isolated auxiliary-module experiment.
// @category BlurResearch
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.nio.file.*;

public class ExportPinnedRecompListings extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2 || !args[1].matches("[0-9a-f]{64}"))
            throw new IllegalArgumentException("Expected output directory and exact SHA-256");
        if (!args[1].equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Input SHA-256 mismatch; refusing address reuse");
        Path out = Path.of(args[0], currentProgram.getName());
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
                index.printf("%s\t%s\t%d\tassembly\tAutomatic boundaries; runtime behavior unverified%n",
                    address, f.getName(), f.getBody().getNumAddresses());
                count++;
            }
        }
        Files.writeString(out.resolve("summary.txt"), "sha256=" + args[1] + "\ndiscovered=" + count +
            "\nAssembly export, not recovered source or complete coverage.\n");
        println("Exported " + count + " hash-verified assembly listings");
    }
}
