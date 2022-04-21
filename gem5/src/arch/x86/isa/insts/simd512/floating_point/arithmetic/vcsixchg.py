microcode = '''

def macroop VCSIXCHG_ZMM_ZMM {
    movfp512 dest=xmm2, src1=xmm0, dataSize=64
    ldfp512phys xmm0, seg, sib, "DISPLACEMENT + (1UL << 42)", dataSize=64
    vcsicompare src1=xmm0, src2=xmm2, dest=xmm2
    movfp512 dest=xmm0, src1=xmm2, dataSize=64
    br label("notcopy"), flags=(nCZF,)
    movfp512 dest=xmm0, src1=xmm1, dataSize=64
    stfp512phys xmm0, seg, sib, "DISPLACEMENT + (1UL << 42)", dataSize=64
notcopy:
    fault "NoFault"
};

def macroop VCSIXCHG_ZMM_M {
    movfp512 dest=xmm2, src1=xmm0, dataSize=64
    ldfp512phys xmm0, seg, sib, "DISPLACEMENT + (1UL << 42)", dataSize=64
    vcsicompare src1=xmm0, src2=xmm2, dest=xmm2
    movfp512 dest=xmm0, src1=xmm2, dataSize=64
    br label("notcopy"), flags=(nCZF,)
    movfp512 dest=xmm0, src1=xmm1, dataSize=64
    stfp512phys xmm0, seg, sib, "DISPLACEMENT + (1UL << 42)", dataSize=64
notcopy:
    fault "NoFault"
};

def macroop VCSIXCHG_ZMM_P {
    movfp512 dest=xmm2, src1=xmm0, dataSize=64
    ldfp512phys xmm0, seg, sib, "DISPLACEMENT + (1UL << 42)", dataSize=64
    vcsicompare src1=xmm0, src2=xmm2, dest=xmm2
    movfp512 dest=xmm0, src1=xmm2, dataSize=64
    br label("notcopy"), flags=(nCZF,)
    movfp512 dest=xmm0, src1=xmm1, dataSize=64
    stfp512phys xmm0, seg, sib, "DISPLACEMENT + (1UL << 42)", dataSize=64
notcopy:
    fault "NoFault"
};

'''

# the br is also used in general_purpose/data_transfer/move.py
# and defined in seqop.isa

# fault "NoFault" is a NOP, see no_operation.py