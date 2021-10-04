#ifndef _G4OCCTSOLID_HH_
#define _G4OCCTSOLID_HH_

#include <G4VSolid.hh>

#include <TopoDS_Solid.hxx>

class G4OCCTSolid: public G4VSolid
{
  public:
    G4OCCTSolid(const TopoDS_Solid& solid);
    virtual ~G4OCCTSolid() = default;

    G4bool CalculateExtent(const EAxis pAxis,
                                   const G4VoxelLimits& pVoxelLimit,
                                   const G4AffineTransform& pTransform,
                                   G4double& pMin, G4double& pMax) const { G4cout << "not implemented" << G4endl; };
      // Calculate the minimum and maximum extent of the solid, when under the
      // specified transform, and within the specified limits. If the solid
      // is not intersected by the region, return false, else return true.

    EInside Inside(const G4ThreeVector& p) const override { G4cout << "not implemented" << G4endl; };
      // Returns kOutside if the point at offset p is outside the shapes
      // boundaries plus Tolerance/2, kSurface if the point is <= Tolerance/2
      // from a surface, otherwise kInside.

    G4ThreeVector SurfaceNormal(const G4ThreeVector& p) const  { G4cout << "not implemented" << G4endl; };
      // Returns the outwards pointing unit normal of the shape for the
      // surface closest to the point at offset p.

    G4double DistanceToIn(const G4ThreeVector& p,
                                  const G4ThreeVector& v) const  { G4cout << "not implemented" << G4endl; };
      // Return the distance along the normalised vector v to the shape,
      // from the point at offset p. If there is no intersection, return
      // kInfinity. The first intersection resulting from `leaving' a
      // surface/volume is discarded. Hence, it is tolerant of points on
      // the surface of the shape.

    G4double DistanceToIn(const G4ThreeVector& p) const  { G4cout << "not implemented" << G4endl; };
      // Calculate the distance to the nearest surface of a shape from an
      // outside point. The distance can be an underestimate.

    G4double DistanceToOut(const G4ThreeVector& p,
                                   const G4ThreeVector& v,
                                   const G4bool calcNorm=false,
                                   G4bool* validNorm = nullptr,
                                   G4ThreeVector* n = nullptr) const  { G4cout << "not implemented" << G4endl; };
      // Return the distance along the normalised vector v to the shape,
      // from a point at an offset p inside or on the surface of the shape.
      // Intersections with surfaces, when the point is < Tolerance/2 from a
      // surface must be ignored.
      // If calcNorm==true:
      //    validNorm set true if the solid lies entirely behind or on the
      //              exiting surface.
      //    n set to exiting outwards normal vector (undefined Magnitude).
      //    validNorm set to false if the solid does not lie entirely behind
      //              or on the exiting surface
      // If calcNorm==false:
      //    validNorm and n are unused.
      //
      // Must be called as solid.DistanceToOut(p,v) or by specifying all
      // the parameters.

    G4double DistanceToOut(const G4ThreeVector& p) const  { G4cout << "not implemented" << G4endl; };
      // Calculate the distance to the nearest surface of a shape from an
      // inside point. The distance can be an underestimate.

    G4double GetCubicVolume() {
      GProp_GProps gprops;
      BRepGProp::VolumeProperties(shape, gprops);
      double volume = gprops.Mass();
    };
      // Returns an estimation of the solid volume in internal units.
      // This method may be overloaded by derived classes to compute the
      // exact geometrical quantity for solids where this is possible,
      // or anyway to cache the computed value.
      // Note: the computed value is NOT cached.

    G4double GetSurfaceArea() {
      GProp_GProps gprops;
      BRepGProp::SurfaceProperties(shape, gprops);
      double volume = gprops.Mass();
    };
      // Return an estimation of the solid surface area in internal units.
      // This method may be overloaded by derived classes to compute the
      // exact geometrical quantity for solids where this is possible,
      // or anyway to cache the computed value.
      // Note: the computed value is NOT cached.

    G4GeometryType  GetEntityType() const  { G4cout << "not implemented" << G4endl; };
      // Provide identification of the class of an object.
      // (required for persistency and STEP interface)

    std::ostream& StreamInfo(std::ostream& os) const  { G4cout << "not implemented" << G4endl; };
      // Dumps contents of the solid to a stream.
    inline void DumpInfo() const;
      // Dumps contents of the solid to the standard output.

    // Visualization functions

    void DescribeYourselfTo (G4VGraphicsScene& scene) const  { G4cout << "not implemented" << G4endl; };
      // A "double dispatch" function which identifies the solid
      // to the graphics scene.


  private:
    TopoDS_Solid fSolid;
};

#endif
