// Export local disassembly, references and inferred pseudocode for the exact baseline.
// @category BlurResearch
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ArchiveEvidence extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (!"dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f"
                .equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Archive targets require the documented Blur.exe baseline");
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("Program: " + currentProgram.getName());
            out.println("SHA256: " + currentProgram.getExecutableSHA256());
            out.println("Language: " + currentProgram.getLanguageID());
            out.println("Image base: " + currentProgram.getImageBase());
            DecompInterface dec = new DecompInterface();
            dec.openProgram(currentProgram);
            for (int i = 1; i < args.length; ++i) {
                Address a = toAddr(args[i]);
                out.println("\nTARGET " + a + " RVA " + a.subtract(currentProgram.getImageBase()));
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    out.println("XREF " + r + " caller " + getFunctionContaining(r.getFromAddress()));
                }
                Function f = getFunctionAt(a);
                if (f == null) { out.println("No function at target"); continue; }
                out.println("FUNCTION " + f.getName() + " " + f.getBody());
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true))
                    out.println(ins.getAddress() + " " + ins);
                out.println("INFERRED PSEUDOCODE (not recovered source; types unverified):");
                DecompileResults result = dec.decompileFunction(f, 60, monitor);
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                else out.println(result.getErrorMessage());
            }
            dec.dispose();
        }
    }
}
