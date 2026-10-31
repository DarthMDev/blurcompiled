// Exact pinned containing-owner audit; no mutation of the reference executable.
// @category BlurResearch
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.io.PrintWriter;
import java.util.*;
public class AuditContainingOwner extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(args.length!=2 || !currentProgram.getExecutableSHA256().equals("dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f")) throw new IllegalArgumentException("Pinned executable required");
  Path out=Path.of(args[0]);Files.createDirectory(out);
  long start=0xb57e60L,end=0xb5a767L,site=0xb57e8cL;
  List<Long> targets=new ArrayList<>();
  for(int k=0;k<32;k++) {
   int index=Byte.toUnsignedInt(getByte(toAddr(0xb5a788L+k)));
   if(index>7)throw new IllegalArgumentException("Index escapes eight-slot table");
   long target=Integer.toUnsignedLong(getInt(toAddr(0xb5a768L+4*index)));
   if(target<start || target>=end)throw new IllegalArgumentException("Escaping target");targets.add(target);
  }
  Deque<Long> pending=new ArrayDeque<>();pending.add(start);
  TreeMap<Long,Instruction> seen=new TreeMap<>();TreeSet<String> ownership=new TreeSet<>();
  while(!pending.isEmpty()) {
   long va=pending.removeLast();
   while(!seen.containsKey(va)) {
    monitor.checkCancelled();if(va<start || va>=end || seen.size()>=4096)throw new IllegalArgumentException("Escaping CFG/budget at "+Long.toHexString(va));
    Address a=toAddr(va);Function f=getFunctionContaining(a);
    ownership.add(f==null?"unowned":f.getEntryPoint()+"\t"+f.getBody());
    if(f!=null && f.getEntryPoint().getOffset()!=start)throw new IllegalArgumentException("Foreign owner "+f);
    disassemble(a);Instruction ins=getInstructionAt(a);
    if(ins==null || ins.getMnemonicString().equals("INT3") || va+ins.getLength()>end)throw new IllegalArgumentException("Invalid boundary");
    seen.put(va,ins);
    if(ins.getFlowType().isJump()) {
     if(va==site)pending.addAll(targets);
     else if(va==0xb58732L) {
      for(int k=0;k<4;k++) {long target=Integer.toUnsignedLong(getInt(toAddr(0xb5a7a8L+4*k)));
       if(target<start || target>=end)throw new IllegalArgumentException("Escaping nested table");pending.add(target);}
     }
     else {if(ins.getFlowType().isComputed() || ins.getFlows().length==0)throw new IllegalArgumentException("Unknown indirect jump at "+a);for(Address b:ins.getFlows())pending.add(b.getOffset());}
    }
    Address next=ins.getFallThrough();if(next==null)break;va=next.getOffset();
   }
  }
  try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(out.resolve("00b57e60.asm")));PrintWriter detail=new PrintWriter(Files.newBufferedWriter(out.resolve("instructions.tsv")))) {
   for(Instruction ins:seen.values()) {asm.printf("%s  %s%n",ins.getAddress(),ins);detail.printf("%s\t%s\t%s%n",ins.getAddress(),java.util.HexFormat.of().formatHex(ins.getBytes()),ins);}
  }
  Files.write(out.resolve("ownership.txt"),ownership);
  Files.writeString(out.resolve("evidence.txt"),"sha256="+currentProgram.getExecutableSHA256()+"\nva=00b57e60\nrva=00757e60\nbound_end=00b5a767\ninstructions="+seen.size()+"\nplan_sha256="+args[1]+"\nIndependent bounded Ghidra CFG; exact 32 byte-remapped table cases; no foreign function ownership. High static CFG confidence; behavior unverified.\n");
  println("AUDIT exported "+seen.size()+" instructions; owners="+ownership.size());
 }
}
