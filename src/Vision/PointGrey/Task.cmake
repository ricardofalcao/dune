if(DUNE_SYS_HAS___ARM_ARCH_7A__)

if(NOT DUNE_SYS_HAS_OPENCV)
  set(TASK_ENABLED FALSE)
endif(NOT DUNE_SYS_HAS_OPENCV)

if(NOT DUNE_SYS_HAS_EXIV2)
  set(TASK_ENABLED FALSE)
endif(NOT DUNE_SYS_HAS_EXIV2)

if(NOT DUNE_SYS_HAS_FLYCAPTURE)
  set(TASK_ENABLED FALSE)
endif(NOT DUNE_SYS_HAS_FLYCAPTURE)

endif(DUNE_SYS_HAS___ARM_ARCH_7A__)

set(TASK_LICENSE "Proprietary")
