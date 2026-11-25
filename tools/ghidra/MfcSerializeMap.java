// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// For each MFC class name given, locate its CRuntimeClass, follow CreateObject to the
// vtable, and decompile Serialize (vtable slot 2) plus the base-class chain.
// Args: <outDir> <ClassA,ClassB,...>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;

public class MfcSerializeMap extends GhidraScript {
  Memory mem;
  DecompInterface dec;

  long u32(Address a) throws Exception { return mem.getInt(a) & 0xffffffffL; }
  Address A(long v) { return toAddr(v); }

  String cstr(Address a) throws Exception {
    StringBuilder sb = new StringBuilder();
    for (int i = 0; i < 128; i++) { byte b = mem.getByte(a.add(i)); if (b == 0) break; sb.append((char) b); }
    return sb.toString();
  }

  /** Find the CRuntimeClass whose m_lpszClassName points at the exact string `name`. */
  Address findRtc(String name) throws Exception {
    byte[] pat = (name + "\0").getBytes("ASCII");
    Address s = mem.findBytes(currentProgram.getMinAddress(), pat, null, true, monitor);
    while (s != null) {
      byte[] ptr = new byte[4];
      long v = s.getOffset();
      for (int i = 0; i < 4; i++) ptr[i] = (byte) (v >> (8 * i));
      Address r = mem.findBytes(currentProgram.getMinAddress(), ptr, null, true, monitor);
      while (r != null) {
        long size = u32(r.add(4)), create = u32(r.add(12));
        boolean byteBefore = mem.getByte(s.subtract(1)) == 0;
        if (byteBefore && size > 0 && size < 0x100000 && getFunctionAt(A(create)) != null) return r;
        if (byteBefore && size > 0 && size < 0x100000 && create != 0 && mem.getBlock(A(create)) != null
            && mem.getBlock(A(create)).isExecute()) return r;
        r = mem.findBytes(r.add(1), ptr, null, true, monitor);
      }
      s = mem.findBytes(s.add(1), pat, null, true, monitor);
    }
    return null;
  }

  /** Walk CreateObject and the constructors it calls; the last "mov [reg], vtable" wins (most-derived). */
  Address findVtable(Address fn, int depth, Set<Address> seen) throws Exception {
    if (fn == null || depth > 3 || !seen.add(fn)) return null;
    Function f = getFunctionAt(fn);
    if (f == null) f = createFunction(fn, null);
    if (f == null) return null;
    Address vt = null;
    InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
    while (it.hasNext()) {
      Instruction ins = it.next();
      String m = ins.getMnemonicString();
      if (m.equals("CALL")) {
        Address[] fl = ins.getFlows();
        if (fl.length == 1) { Address v2 = findVtable(fl[0], depth + 1, seen); if (v2 != null && vt == null) vt = v2; }
      } else if (m.equals("MOV") && ins.getNumOperands() == 2 && ins.getDefaultOperandRepresentation(0).startsWith("dword ptr [")
          && ins.getDefaultOperandRepresentation(0).matches("dword ptr \\[E[A-Z]{2}\\]")) {
        Object[] o = ins.getOpObjects(1);
        if (o.length == 1 && o[0] instanceof Scalar) {
          Address c = A(((Scalar) o[0]).getUnsignedValue());
          MemoryBlock b = mem.getBlock(c);
          if (b != null && !b.isExecute() && b.getName().equals(".rdata")) vt = c; // keep last in this function
        }
      }
    }
    return vt;
  }

  public void run() throws Exception {
    String[] args = getScriptArgs();
    File out = new File(args[0]); out.mkdirs();
    mem = currentProgram.getMemory();
    dec = new DecompInterface(); dec.openProgram(currentProgram);
    PrintWriter idx = new PrintWriter(new File(out, "index.tsv"));
    idx.println("class\trtc\tsize\tschema\tbase\tcreate\tvtable\tserialize");
    Set<String> done = new HashSet<>();
    Deque<String> todo = new ArrayDeque<>(Arrays.asList(args[1].split(",")));
    while (!todo.isEmpty()) {
      String name = todo.poll();
      if (!done.add(name)) continue;
      Address rtc = findRtc(name);
      if (rtc == null) { idx.println(name + "\t-"); println("no RTC for " + name); continue; }
      long size = u32(rtc.add(4)), schema = u32(rtc.add(8)) & 0xffff, create = u32(rtc.add(12)), baseP = u32(rtc.add(16));
      String base = "";
      if (baseP != 0 && mem.getBlock(A(baseP)) != null) {
        try { base = cstr(A(u32(A(baseP)))); } catch (Exception e) { base = "?"; }
        if (!base.isEmpty() && !base.equals("CObject")) todo.add(base);
      }
      Address vt = create != 0 ? findVtable(A(create), 0, new HashSet<>()) : null;
      Address ser = vt != null ? A(u32(vt.add(8))) : null;
      idx.println(name + "\t" + rtc + "\t" + size + "\t" + schema + "\t" + base + "\t" + Long.toHexString(create) + "\t" + vt + "\t" + ser);
      if (ser != null) {
        Function f = getFunctionAt(ser);
        if (f == null) f = createFunction(ser, null);
        if (f != null) {
          try { f.setName(name + "_Serialize", ghidra.program.model.symbol.SourceType.USER_DEFINED); } catch (Exception e) {}
          DecompileResults r = dec.decompileFunction(f, 120, monitor);
          try (PrintWriter w = new PrintWriter(new File(out, name + ".Serialize.c"))) {
            w.println("// " + name + "::Serialize @ " + ser + "  size=" + size + " schema=" + schema + " base=" + base);
            w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + r.getErrorMessage());
          }
        }
      }
    }
    idx.close();
  }
}
