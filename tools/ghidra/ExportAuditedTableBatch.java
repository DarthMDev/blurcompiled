// Recover bounded CFGs using independently read finite jump tables.
// @category BlurResearch
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.io.PrintWriter;
import java.util.*;

public class ExportAuditedTableBatch extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3 || !args[2].equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected plan, fresh output, pinned executable SHA");
        Path out = Path.of(args[1]);
        if(Files.exists(out)) throw new IllegalArgumentException("Fresh output required");
        Files.createDirectory(out);
        List<String> results = new ArrayList<>();
        for (String line : Files.readAllLines(Path.of(args[0]))) {
            String[] row = line.split("\t", -1);
            String label = row[0];
            try {
                if (row.length != 6) throw new IllegalArgumentException("Invalid plan row");
                long start = Long.parseLong(row[0],16), end = Long.parseLong(row[1],16);
                if (end <= start || !((row[2].equals("8192") || row[2].equals("16384")) && end-start <= Integer.parseInt(row[2]))) throw new IllegalArgumentException("Invalid bounds");
                Address entry = toAddr(start);
                Function owner = getFunctionContaining(entry);
                if (owner != null && !owner.getEntryPoint().equals(entry))
                    throw new IllegalArgumentException("Entry has a different owner");
                Map<Long,List<Long>> tables = new HashMap<>();
                if (!row[3].isEmpty()) for (String table : row[3].split(";")) {
                    String[] t = table.split(":");
                    if(t.length!=3 && t.length!=4) throw new IllegalArgumentException("Invalid table shape");
                    long site=Long.parseLong(t[0],16), base=Long.parseLong(t[1],16);
                    int count=Integer.parseInt(t[2]);
                    if (count<=0 || count>256 || site<start || site>=end)
                        throw new IllegalArgumentException("Invalid finite table");
                    List<Long> targets=new ArrayList<>();
                    for(int k=0;k<count;++k) {
                        int index=t.length==4 ? Byte.toUnsignedInt(getByte(toAddr(Long.parseLong(t[3],16)+k))) : k;
                        long target=Integer.toUnsignedLong(getInt(toAddr(base+4L*index)));
                        if(target<start || target>=end) throw new IllegalArgumentException("Escaping table target");
                        targets.add(target);
                    }
                    tables.put(site,targets);
                }
                Deque<Long> pending=new ArrayDeque<>(); pending.add(start);
                if (!row[4].isEmpty()) for(String value:row[4].split(",")) pending.add(Long.parseLong(value,16));
                TreeMap<Long,Instruction> seen=new TreeMap<>();
                while(!pending.isEmpty()) {
                    monitor.checkCancelled(); long va=pending.removeLast();
                    while(!seen.containsKey(va)) {
                        if(va<start || va>=end || seen.size()>=4096)
                            throw new IllegalArgumentException("CFG escapes bound/budget");
                        Address a=toAddr(va);
                        Function other=getFunctionContaining(a);
                        if(other!=null && !other.getEntryPoint().equals(entry))
                            throw new IllegalArgumentException("CFG intersects other function "+other);
                        if(!currentProgram.getMemory().getBlock(a).isExecute())
                            throw new IllegalArgumentException("Nonexecutable instruction");
                        disassemble(a); Instruction ins=getInstructionAt(a);
                        if(ins==null || va+ins.getLength()>end || ins.getMnemonicString().equals("INT3"))
                            throw new IllegalArgumentException("Invalid instruction boundary");
                        seen.put(va,ins);
                        if(ins.getFlowType().isJump()) {
                            if(tables.containsKey(va)) pending.addAll(tables.get(va));
                            else {
                                Address[] flows=ins.getFlows();
                                if(ins.getFlowType().isComputed() || flows.length==0)
                                    throw new IllegalArgumentException("Unaudited indirect jump");
                                for(Address target:flows) pending.add(target.getOffset());
                            }
                        }
                        Address next=ins.getFallThrough();
                        if(next==null) break;
                        va=next.getOffset();
                    }
                }
                // Every table site must actually belong to the reached body.
                if(!seen.keySet().containsAll(tables.keySet()))
                    throw new IllegalArgumentException("Unreached planned table site");
                Path dir=out.resolve("observed-"+label); Files.createDirectory(dir);
                try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(dir.resolve(label+".asm")))) {
                    for(Instruction ins:seen.values()) asm.printf("%s  %s%n",ins.getAddress(),ins.toString());
                }
                try(PrintWriter bytes=new PrintWriter(Files.newBufferedWriter(dir.resolve("instructions.tsv")))) {
                    for(Instruction ins:seen.values()) bytes.printf("%s\t%s\t%s%n",ins.getAddress(), java.util.HexFormat.of().formatHex(ins.getBytes()),ins);
                }
                Files.writeString(dir.resolve("evidence.txt"),"sha256="+args[2]+"\nva="+row[0]+"\nrva="+
                    Long.toHexString(start-currentProgram.getImageBase().getOffset())+"\ninstructions="+seen.size()+
                    "\nbound_end="+row[1]+"\nplan_sha256="+row[5]+
                    "\nBounded CFG from Ghidra instructions and explicit flat/byte-remapped PE targets; no foreign owner; behavior unverified.\n");
                results.add(label+"\texported\t"+seen.size());
            } catch(Exception error) {
                results.add(label+"\treview\t"+error.toString().replace('\n',' ').replace('\t',' '));
            }
            Files.write(out.resolve("results.tsv"),results);
        }
    }
}
