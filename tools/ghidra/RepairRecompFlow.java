// Seed bounded switch targets observed in the pinned executable, then refresh
// Ghidra's candidate function bodies. This changes only the ignored database.
// @category BlurResearch
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.util.TreeSet;

public class RepairRecompFlow extends GhidraScript {
    public void run() throws Exception {
        if (!"dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f"
                .equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Wrong Blur executable baseline");
        TreeSet<Long> targets = new TreeSet<>();
        // Bounds from CMP/JA before each indirect jump; addresses are guest VAs.
        long[][] tables = {{0x48cd2c, 4}, {0x70578c, 4}, {0xaebbac, 7}, {0xb15a20, 8}};
        for (long[] table : tables)
            for (int i = 0; i < table[1]; ++i)
                targets.add(Integer.toUnsignedLong(getInt(toAddr(table[0] + 4 * i))));
        // CMP 0x33 bounds the byte selector, which indexes a second table.
        for (int i = 0; i < 52; ++i) {
            int selector = Byte.toUnsignedInt(getByte(toAddr(0xc654c8 + i)));
            if (selector > 9) throw new IllegalArgumentException("Unexpected switch selector");
            targets.add(Integer.toUnsignedLong(getInt(toAddr(0xc654a0 + 4 * selector))));
        }
        targets.add(0xaea35cL); // direct jump at 0xaea356
        targets.add(0xb15a04L); // direct conditional branches in the 0xb154b0 region
        targets.add(0x48cb40L); // kit-recovered candidate entry; verify decoding
        for (long target : targets) {
            Address address = toAddr(target);
            if (!currentProgram.getMemory().getBlock(address).isExecute())
                throw new IllegalArgumentException("Switch target outside executable section");
            disassemble(address);
            if (getInstructionAt(address) == null)
                throw new IllegalArgumentException("Could not decode target " + address);
            // Ghidra does not infer these switch edges. Give an otherwise
            // unowned block a candidate entry so the assembly exporter carries
            // it; recomp-kit handles jumps between these blocks explicitly.
            if (getFunctionContaining(address) == null && createFunction(address, null) == null)
                throw new IllegalArgumentException("Could not export unowned block " + address);
            println("Seeded candidate block VA " + address + " RVA " + address.subtract(currentProgram.getImageBase()));
        }
        for (long entry : new long[]{0x48cb40, 0x705330, 0xaeb9c0, 0xb154b0, 0xc65220, 0xaea310}) {
            Address address = toAddr(entry);
            Function f = getFunctionAt(address);
            if (f == null) f = createFunction(address, null);
            if (f == null) throw new IllegalArgumentException("Could not create candidate function " + address);
            CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
            println("Refreshed automatic body " + address + " " + f.getBody());
        }
    }
}
