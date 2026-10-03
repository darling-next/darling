import pathlib, re
F = pathlib.Path('threads.c'); t = F.read_text()
m = re.search(r'^([ \t]*)int checked_in = 0;\n', t, re.M)
assert m, 'checked_in decl'
ind = m.group(1)
t = t[:m.end()] + (ind + "/* THE SEQUENCE THIS THREAD PUBLISHED (dar-4cp9). MEASURED: the server recorded the checkin for the very\n"
                   + ind + " * tid that then aborted -- [srv-checkin #32 pid=... tid=3124771 ...] immediately before\n"
                   + ind + " * [rpc-socket-DENIED ... call=checkin image=loader] and sigexc-fatal -- so the operation had been\n"
                   + ind + " * delivered and only the client's acceptance test lost its reply to a later publisher, exactly as on the\n"
                   + ind + " * checkout path (pub=5 seen=17). A checkin that was published is a checkin the server has. */\n"
                   + ind + "uint32_t checkin_pubseq = 0;\n") + t[m.end():]
m2 = re.search(r'^([ \t]*)uint32_t mine = __atomic_add_fetch\(&checkin_seq, 1, __ATOMIC_RELAXED\);\n', t, re.M)
assert m2, 'mine line'
t = t[:m2.end()] + m2.group(1) + "checkin_pubseq = mine;\n" + t[m2.end():]
m3 = re.search(r'^([ \t]*)\} else if \(claimed\) \{\n', t, re.M)
assert m3, 'accept'
ind3 = m3.group(1)
t = t[:m3.start()] + (ind3 + "} else if (claimed || checkin_pubseq != 0) {\n"
    + ind3 + "\tif (!claimed) {\n"
    + ind3 + "\t\tfprintf(stderr, \"[checkin-reply-unseen tid=%d pub=%u seen=%u seenstate=0x%x -- published, reply overtaken]\\n\",\n"
    + ind3 + "\t\t\t(int)syscall(SYS_gettid), (unsigned)checkin_pubseq, (unsigned)seenSeq, (unsigned)seenState);\n"
    + ind3 + "\t}\n" + t[m3.end():])
F.write_text(t); print('checkin accept relaxed')
