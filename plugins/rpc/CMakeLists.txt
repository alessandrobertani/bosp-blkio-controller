
if (CONFIG_BBQUE_RPC_FIFO)
    add_subdirectory(fifo)
endif (CONFIG_BBQUE_RPC_FIFO)

if (CONFIG_BBQUE_RPC_PB_FIFO)
    add_subdirectory(pb_fifo)
endif (CONFIG_BBQUE_RPC_PB_FIFO)
