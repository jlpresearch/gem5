from m5.params import *
from m5.proxy import *
from m5.objects.ClockedObject import ClockedObject

class GUPSGen(ClockedObject):
    type = 'GUPSGen'
    cxx_header = "cpu/testers/traffic_gen/gups_gen.hh"

    system = Param.System(Parent.any, 'System this generator is a part of')

    port = RequestPort('Port that should be connected to other components')

    block_size = Param.Addr(64, 'The atom size for memory it is connected to')

    start_addr = Param.Addr(0, 'Start address for allocating update table,'
                                ' should be a multiple of block_size')

    mem_size = Param.Addr(0x100000, 'Size for allocating update table,'
                                    ' should be a power of 2')
