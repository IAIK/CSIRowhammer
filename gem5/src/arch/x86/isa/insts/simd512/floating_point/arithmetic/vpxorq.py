microcode = '''
def macroop VPXORQ_ZMM_ZMM {
    vxori dest=xmm0, src1=xmm0v, src2=xmm0m, size=4, VL=64
};

def macroop VPXORQ_ZMM_M {
    ldfp512 ufp1, seg, sib, "DISPLACEMENT + 0", dataSize=64
    vxori dest=xmm0, src1=xmm0v, src2=ufp1, size=4, VL=64
};

def macroop VPXORQ_ZMM_P {
    panic "VPXORQ_ZMM_P is not implemented."
};


'''