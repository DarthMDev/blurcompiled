// Recover one runtime-observed entry in a disposable copy of a Ghidra project.
// @category BlurResearch
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.nio.file.*;

public class ExportObservedFunction extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if ((args.length < 3 || args.length > 5) || !args[1].matches("[0-9a-f]{64}") ||
                !args[2].matches("[0-9a-f]{8}"))
            throw new IllegalArgumentException("Expected fresh output directory, SHA-256, guest VA, optional bounded redisassembly end and audited tail entries");
        if (!args[1].equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Executable identity changed");
        Path out = Path.of(args[0]);
        Files.createDirectory(out); // Preserve earlier evidence; never overwrite.
        Address entry = toAddr(Long.parseLong(args[2], 16));
        if (currentProgram.getMemory().getBlock(entry) == null ||
                !currentProgram.getMemory().getBlock(entry).isExecute())
            throw new IllegalArgumentException("Observed target is not executable");
        Function owner = getFunctionContaining(entry);
        if (owner != null && !owner.getEntryPoint().equals(entry))
            throw new IllegalArgumentException("Target belongs to another function: " + owner);
        if (args.length >= 4) {
            if (!args[3].matches("[0-9a-f]{8}"))
                throw new IllegalArgumentException("Invalid redisassembly end");
            Address end = toAddr(Long.parseLong(args[3], 16));
            if (end.subtract(entry) < 0 || end.subtract(entry) >= 4096 ||
                    currentProgram.getMemory().getBlock(end) != currentProgram.getMemory().getBlock(entry))
                throw new IllegalArgumentException("Redisassembly exceeds one bounded executable region");
            FunctionIterator others = currentProgram.getFunctionManager().getFunctions(entry, true);
            while (others.hasNext()) {
                Address other = others.next().getEntryPoint();
                if (other.compareTo(end) > 0) break;
                if (!other.equals(entry))
                    throw new IllegalArgumentException("Redisassembly would erase another function: " + other);
            }
            if (owner != null) removeFunction(owner);
            clearListing(entry, end);
        }
        if (args.length == 5) {
            // A tail thunk's body must not absorb an already-audited callee.
            // Establish that entry in this disposable DB before creating the
            // observed thunk. Baseline exports and the original DB stay intact.
            for (String value : args[4].split(",")) {
                if (!value.matches("[0-9a-f]{8}"))
                    throw new IllegalArgumentException("Invalid tail entry");
                Address tail = toAddr(Long.parseLong(value, 16));
                Address boundedEnd = toAddr(Long.parseLong(args[3], 16));
                if ((tail.compareTo(entry) >= 0 && tail.compareTo(boundedEnd) <= 0) ||
                        currentProgram.getMemory().getBlock(tail) == null ||
                        !currentProgram.getMemory().getBlock(tail).isExecute())
                    throw new IllegalArgumentException("Tail must be outside the bounded thunk and executable");
                Function tailOwner = getFunctionContaining(tail);
                if (tailOwner != null && !tailOwner.getEntryPoint().equals(tail))
                    throw new IllegalArgumentException("Tail belongs to another function");
                disassemble(tail);
                Function target = getFunctionAt(tail);
                if (target == null) target = createFunction(tail, null);
                if (target == null) throw new IllegalArgumentException("Tail entry recovery failed");
                CreateFunctionCmd.fixupFunctionBody(currentProgram, target, monitor);
            }
        }
        disassemble(entry);
        Function f = getFunctionAt(entry);
        if (f == null) f = createFunction(entry, null);
        if (f == null) throw new IllegalArgumentException("Function recovery failed");
        CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
        int count = 0;
        try (PrintWriter asm = new PrintWriter(Files.newBufferedWriter(out.resolve(args[2] + ".asm")))) {
            for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                monitor.checkCancelled();
                asm.printf("%s  %s%n", ins.getAddress(), ins.toString());
                count++;
            }
        }
        if (count == 0) throw new IllegalArgumentException("Empty recovered listing");
        Files.writeString(out.resolve("evidence.txt"), "sha256=" + args[1] +
            "\nva=" + entry + "\nrva=" + Long.toHexString(entry.subtract(currentProgram.getImageBase())) +
            "\ninstructions=" + count + "\nbody=" + f.getBody() +
            "\nAutomatic recovery of observed entry; semantics and completeness unverified.\n");
        println("Exported observed entry " + entry + ": " + count + " instructions");
    }
}
