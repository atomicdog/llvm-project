import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
from lldbsuite.test.gdbclientutils import *
from lldbsuite.test.lldbgdbclient import GDBRemoteTestBase

# Unwinding an HCS08 stack out of .debug_frame.
#
# hcs08.yaml is a real linked image - the three-frame program from the
# backend's own CFI test, built with -g -O0 - and the registers and stack bytes
# below are what an HCS08 simulator actually had when it halted three frames
# deep in that binary. Both halves come from the same build, so the addresses
# the unwinder is expected to recover are the ones that were genuinely on the
# stack rather than ones worked out by hand.
#
# What this is really defending is one number. The CFA on this target is the
# caller's SP, which is SP+2 on entry, and the return address sits at **CFA-1**
# rather than the CFA-2 that every ordinary machine would use: SP points one
# byte *below* the last thing pushed, so `jsr` leaves PCL at the CFA itself and
# PCH one below it, and the two bytes read high-first begin at CFA-1. Getting
# that wrong does not fail loudly - it reports a caller one byte off, which
# lands in a plausible-looking wrong place - so it takes a walk over a real
# stack to catch. Two steps out, not one: a single frame can come out right
# from a rule whose errors happen to cancel.

# leaf() halted here, and the two frames it was called from.
PC_LEAF = 0x802D
PC_MIDDLE = 0x8064
PC_RUN = 0x8092
SP_LEAF = 0x07DF

# CFA is the caller's SP: leaf's frame is 10 bytes, middle's is 20.
CFA_LEAF = 0x07E9
CFA_MIDDLE = 0x07FD

STACK_ADDR = 0x07E0
STACK = bytes.fromhex(
    "0015002a002a00158064002a0017001700410014001500160017001480928007"
)


class MyResponder(MockGDBServerResponder):
    def qSupported(self, client_supported):
        return "PacketSize=4000"

    # No qXfer:features:read, so LLDB falls back to the built-in HCS08 register
    # set: a, h, x, sp, pc, ccr, in that order. These are the values the
    # simulator reported at the halt.
    def haltReason(self):
        return "T0500:00;01:00;02:2a;03:07df;04:802d;05:60;"

    def cont(self):
        return self.haltReason()

    def readMemory(self, addr, length):
        # Everything below the captured window really was zero in this run, and
        # zero is what the base class returns for it.
        out = ""
        for i in range(length):
            byte = addr + i - STACK_ADDR
            if 0 <= byte < len(STACK):
                out += "%02x" % STACK[byte]
            else:
                out += "00"
        return out


class TestHCS08Backtrace(GDBRemoteTestBase):
    @skipIfLLVMTargetMissing("HCS08")
    def test(self):
        """Walk an HCS08 stack and check .debug_frame told the truth."""
        target = self.createTarget("hcs08.yaml")
        self.assertTrue(target.GetTriple().startswith("hcs08"))
        self.assertEqual(target.GetAddressByteSize(), 2)
        self.assertEqual(target.GetByteOrder(), lldb.eByteOrderBig)

        self.server.responder = MyResponder()

        if self.TraceOn():
            self.runCmd("log enable gdb-remote packets")
            self.addTearDownHook(lambda: self.runCmd("log disable gdb-remote packets"))

        process = self.connect(target)
        lldbutil.expect_state_changes(
            self, self.dbg.GetListener(), process, [lldb.eStateStopped]
        )
        self.assertEqual(len(process.threads), 1, "Only one thread")
        thread = process.GetThreadAtIndex(0)
        frame = thread.GetFrameAtIndex(0)

        # The whole programmer's model, read back through a big-endian target
        # whose registers are a mix of 1- and 2-byte.
        regs = frame.GetRegisters().GetValueAtIndex(0)
        expected = {
            "a": 0x00,
            "h": 0x00,
            "x": 0x2A,
            "sp": SP_LEAF,
            "pc": PC_LEAF,
            "ccr": 0x60,
        }
        self.assertEqual(len(regs), len(expected))
        for reg in regs:
            self.assertEqual(reg.GetValueAsUnsigned(), expected[reg.GetName()])

        # The backtrace itself. Frame 0 is where we halted; 1 and 2 each cost
        # the unwinder a CFA-1 read it could have got wrong.
        self.assertGreaterEqual(len(thread.frames), 3)
        for idx, (name, pc) in enumerate(
            [("leaf", PC_LEAF), ("middle", PC_MIDDLE), ("run", PC_RUN)]
        ):
            f = thread.GetFrameAtIndex(idx)
            self.assertEqual(f.GetFunctionName(), name, "frame %d function" % idx)
            self.assertEqual(
                f.GetPCAddress().GetLoadAddress(target), pc, "frame %d pc" % idx
            )

        # And the definition those addresses came out of: the CFA is the SP the
        # caller had at the call site, not its lowest occupied byte. Recovering
        # the caller's SP is exactly assigning it the CFA, which is why any
        # other definition walks every parent frame off by one.
        self.assertEqual(thread.GetFrameAtIndex(0).GetCFA(), CFA_LEAF)
        self.assertEqual(thread.GetFrameAtIndex(1).GetCFA(), CFA_MIDDLE)
        self.assertEqual(
            thread.GetFrameAtIndex(1)
            .FindRegister("sp")
            .GetValueAsUnsigned(),
            CFA_LEAF,
            "the caller's SP is this frame's CFA",
        )
