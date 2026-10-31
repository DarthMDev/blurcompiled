// Seed only manually reviewed code targets from archive stream vtables.
// @category BlurResearch
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
public class SeedArchiveFunctions extends GhidraScript {
    public void run() throws Exception {
        if (!"dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f"
                .equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Wrong executable baseline");
        for (String arg : getScriptArgs()) {
            Address a = toAddr(arg);
            if (getFunctionAt(a) == null) {
                disassemble(a);
                println("Seeded " + createFunction(a, null));
            }
        }
    }
}
