// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Args: <outDir> <lo> <hi> <maxDepth> Name=0xaddr ...
// Decompiles each root and every function it calls (recursively, up to maxDepth) whose entry
// lies in [lo, hi), also following function pointers taken as immediates (process callbacks).
// Writes <outDir>/<Name>/<addr>_<funcname>.c and an index of the call tree.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;
public class DecompileTree extends GhidraScript {
  long lo, hi; int maxDepth; DecompInterface dec;
  public void run() throws Exception {
    String[] a = getScriptArgs();
    File out = new File(a[0]); lo = Long.decode(a[1]); hi = Long.decode(a[2]); maxDepth = Integer.parseInt(a[3]);
    dec = new DecompInterface(); dec.openProgram(currentProgram);
    for (int i = 4; i < a.length; i++) {
      String[] kv = a[i].split("=");
      File dir = new File(out, kv[0]); dir.mkdirs();
      PrintWriter idx = new PrintWriter(new File(dir, "_tree.txt"));
      walk(toAddr(kv[1]), 0, dir, idx, new HashSet<>());
      idx.close();
    }
  }
  void walk(Address ep, int depth, File dir, PrintWriter idx, Set<Address> seen) throws Exception {
    if (!seen.add(ep)) { return; }
    Function f = getFunctionAt(ep);
    if (f == null) f = createFunction(ep, null);
    if (f == null) { idx.println("  ".repeat(depth) + ep + " (no function)"); return; }
    idx.println("  ".repeat(depth) + f.getEntryPoint() + " " + f.getName());
    DecompileResults r = dec.decompileFunction(f, 120, monitor);
    try (PrintWriter w = new PrintWriter(new File(dir, f.getEntryPoint() + "_" + f.getName().replace(':', '_') + ".c"))) {
      w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed " + r.getErrorMessage());
    }
    if (depth >= maxDepth) return;
    List<Address> next = new ArrayList<>();
    InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
    while (it.hasNext()) {
      Instruction ins = it.next();
      if (ins.getMnemonicString().equals("CALL"))
        for (Address t : ins.getFlows()) next.add(t);
      for (int op = 0; op < ins.getNumOperands(); op++)
        for (Object o : ins.getOpObjects(op))
          if (o instanceof Scalar) {
            long v = ((Scalar) o).getUnsignedValue();
            if (v >= lo && v < hi && getInstructionAt(toAddr(v)) != null && !ins.getMnemonicString().startsWith("J")) next.add(toAddr(v));
          }
    }
    for (Address t : next) if (t.getOffset() >= lo && t.getOffset() < hi) walk(t, depth + 1, dir, idx, seen);
  }
}
