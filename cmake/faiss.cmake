# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
INCLUDE(ExternalProject)

MESSAGE(STATUS "Adding faiss and its dependencies")
SET(FAISS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/faiss)
SET(FAISS_BUILD_DIR ${CMAKE_BINARY_DIR}/faiss)
SET(FAISS_LIBRARY ${FAISS_BUILD_DIR}/faiss/libfaiss.a)
FIND_LIBRARY(OPENMP_LIBRARY REQUIRED
    NAMES libomp.so omp
    HINTS ${WITH_OPENMP}/lib)
FIND_LIBRARY(BLAS_LIBRARIES REQUIRED
    NAMES libopenblas${PIC_EXT}.a openblas
    HINTS ${WITH_OPENBLAS}/lib)
ExternalProject_Add(
  FAISS_EXTERNAL
  SOURCE_DIR ${FAISS_SOURCE_DIR}
  BINARY_DIR ${FAISS_BUILD_DIR}
  PATCH_COMMAND ${CMAKE_COMMAND} -E echo "applying patch to faiss"
  COMMAND patch --no-backup-if-mismatch -f -r - -p1 -N < ${CMAKE_SOURCE_DIR}/extra/faiss.patch || true
  CMAKE_ARGS -DFAISS_ENABLE_GPU=OFF -DFAISS_ENABLE_RAFT=OFF -DFAISS_ENABLE_PYTHON=OFF -DFAISS_ENABLE_C_API=OFF -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF -DFAISS_OPT_LEVEL=generic -DRAFT_NVTX=OFF -DCMAKE_CUDA_ARCHITECTURES="NATIVE" -DOpenMP_CXX_LIB_NAMES=libomp -DOpenMP_libomp_LIBRARY=${OPENMP_LIBRARY} -DOpenMP_CXX_INCLUDE_DIR=${WITH_OPENMP}/include -DBLAS_LIBRARIES=${BLAS_LIBRARIES}
  BUILD_BYPRODUCTS ${FAISS_LIBRARY}
  INSTALL_COMMAND ""
)
# revert patch in submodule so we have a clean git state
ExternalProject_Add_Step(
  FAISS_EXTERNAL revertpatch
  DEPENDEES install
  COMMAND ${CMAKE_COMMAND} -E echo "reverting faiss patch"
  COMMAND patch --no-backup-if-mismatch -f -r - -p1 -R < ${CMAKE_SOURCE_DIR}/extra/faiss.patch || true
  WORKING_DIRECTORY <SOURCE_DIR>
)
ADD_LIBRARY(FAISS STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET FAISS PROPERTY IMPORTED_LOCATION ${FAISS_LIBRARY})
# run the ExternalProject target
ADD_DEPENDENCIES(FAISS FAISS_EXTERNAL)
