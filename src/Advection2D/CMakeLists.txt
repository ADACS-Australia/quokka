if (AMReX_SPACEDIM GREATER_EQUAL 2)
    add_executable(test_advection2d ../main.cpp test_advection2d.cpp)
    if(AMReX_GPU_BACKEND MATCHES "CUDA")
        setup_target_for_cuda_compilation(test_advection2d)
    endif()
endif()
