from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject
from m5.objects.MemCtrl import *

class CSIMemCtrl(MemCtrl):
    type="CSIMemCtrl"
    cxx_header = "CSIRowhammer/CSIMemCtrl.hh" 
    fail = VectorParam.Int64("0", "Comma seperated list of amounts of reads after which a bit is flipped")
    mac_tick_latency = Param.Latency('1ns', "Latency in <arbitrary time unit> that one MAC calculation takes")