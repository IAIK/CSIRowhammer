microcode = '''

def macroop VCSILD_ZMM_ZMM {
    ldfp512phys xmm0, seg, sib, "DISPLACEMENT + (1UL << 42)", dataSize=64
    ldphys reg, seg, sib, "DISPLACEMENT + (1UL << 41)"
};

def macroop VCSILD_ZMM_M {
    ldfp512phys xmm0, seg, sib, "DISPLACEMENT + (1UL << 42)", dataSize=64
    ldphys reg, seg, sib, "DISPLACEMENT + (1UL << 41)"
};

def macroop VCSILD_ZMM_P {
    ldfp512phys xmm0, seg, sib, "DISPLACEMENT + (1UL << 42)", dataSize=64
    ldphys reg, seg, sib, "DISPLACEMENT + (1UL << 41)"
};

'''