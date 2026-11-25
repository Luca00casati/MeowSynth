// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Args: 0xaddr ...  -> prints functions that reference each address
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.listing.Function;
public class RefFuncs extends GhidraScript {
  public void run() throws Exception {
    for (String a : getScriptArgs())
      for (Reference r : getReferencesTo(toAddr(a))) {
        Function f = getFunctionContaining(r.getFromAddress());
        println("REF " + a + " from " + r.getFromAddress() + " in " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
      }
  }
}
