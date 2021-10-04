# G4OCCT
Geant4 interface to OpenCASCADE geometry (i.e. direct STEP loading)

## Synopsis
This project aims to provide a library that allows loading OpenCASCADE geometries for use in Geant4 simulations. This is accomplished, initially, by implementing the `G4VSolid` pure virtual functions using OpenCASCADE function. The goal is to allow for loading STEP files directly from CAD into Geant4 without the pitfalls that are commonly encountered with CAD tesselation (poor scaling with number of facets, introduction of overlaps with facets).
