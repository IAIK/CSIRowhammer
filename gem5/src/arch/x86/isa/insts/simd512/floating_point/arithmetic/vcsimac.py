microcode = '''
def macroop VCSIMAC_ZMM_ZMM {
    vcsimac src1=xmm0, src2=t0, dest=t1, VL=64
};

def macroop VCSIMAC_ZMM_M {
    vcsimac src1=xmm0, src2=t0, dest=t1, VL=64
};

def macroop VCSIMAC_ZMM_P {
    vcsimac src1=xmm0, src2=t0, dest=t1, VL=64
};
'''