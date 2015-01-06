#include "valhalla/midgard/pointll.h"
#include <valhalla/midgard/util.h>
#include "valhalla/midgard/distanceapproximator.h"
#include "valhalla/midgard/vector2.h"

namespace {
  const float INVALID = 0xBADBADBAD;
}

namespace valhalla {
namespace midgard {

PointLL::PointLL()
    : Point2(INVALID, INVALID) {
}

PointLL::PointLL(float lng, float lat)
    : Point2(lng, lat) {
}

PointLL::PointLL(const PointLL& ll)
    : Point2(ll.lng(), ll.lat()) {
}

float PointLL::lat() const {
  return y_;
}

float PointLL::lng() const {
  return x_;
}

bool PointLL::IsValid() const{
  //is a range check appropriate?
  //return x_ >= -180 && y >= -90 && x_ < 180 && y_ < 90;
  return x_ != INVALID && y_ != INVALID;
}

void PointLL::Invalidate() {
  x_ = INVALID;
  y_ = INVALID;
}

// May want to use DistanceApproximator!
float PointLL::DistanceSquared(const PointLL& ll2) const {
  return sqr(Distance(ll2));
}

float PointLL::Distance(const PointLL& ll2) const {
  // If points are the same, return 0
  if (*this == ll2)
    return 0.0f;

  // Delta longitude. Don't need to worry about crossing 180
  // since cos(x) = cos(-x)
  double deltalng = (ll2.lng() - lng()) * kRadPerDeg;
  double a = lat() * kRadPerDeg;
  double c = ll2.lat() * kRadPerDeg;

  // Find the angle subtended in radians (law of cosines)
  double cosb = (sin(a) * sin(c)) + (cos(a) * cos(c) * cos(deltalng));

  // Angle subtended * radius of earth (portion of the circumference).
  // Protect against cosb being outside -1 to 1 range.
  if (cosb >= 1.0)
    return 0.00001f;
  else if (cosb < -1.0)
    return kPi * kRadEarthKm;
  else
    return (float)(acos(cosb) * kRadEarthKm);
}

float PointLL::Length(const std::vector<PointLL>& pts) const {
  float length = 0;
  for (unsigned int i = 0, n = pts.size(); i < n-1; i++) {
    length += pts[i].Distance(pts[i+1]);
  }
  return length;
}

float PointLL::Curvature(const PointLL& ll1, const PointLL& ll2) const {
  // Find the 3 distances between positions
  float a = Distance(ll1);
  float b = ll1.Distance(ll2);
  float c = Distance(ll2);
  float s = (a + b + c) * 0.5f;
  float k = sqrtf(s * (s - a) * (s - b) * (s - c));
  return ((a * b * c) / (4.0f * k));
}

float PointLL::Heading(const PointLL& ll2) const {
  // If points are the same, return 0
  if (*this == ll2)
    return 0.0f;

  // Convert to radians and compute heading
  float lat1 = lat() * kRadPerDeg;
  float lat2 = ll2.lat() * kRadPerDeg;
  float dlng = (ll2.lng() - lng()) * kRadPerDeg;
  float y = sinf(dlng) * cosf(lat2);
  float x = cosf(lat1) * sinf(lat2) - sinf(lat1) * cosf(lat2) * cosf(dlng);
  float bearing = atan2f(y, x) * kDegPerRad;
  return (bearing < 0.0f) ? bearing + 360.0f : bearing;
}

float PointLL::ClosestPoint(const std::vector<PointLL>& pts, PointLL& closest,
                            int& idx) const {
  // TODO - make sure at least 2 points are in list

  DistanceApproximator approx;
  approx.SetTestPoint(*this);

  // Iterate through the pts
  unsigned int count = pts.size();
  bool beyond_end = true;   // Need to test past the end point?
  Vector2 v1;       // Segment vector (v1)
  Vector2 v2;       // Vector from origin to target (v2)
  PointLL projpt;   // Projected point along v1
  float dot;        // Dot product of v1 and v2
  float comp;       // Component of v2 along v1
  float dist;       // Squared distance from target to closest point on line
  float mindist = 2147483647.0f;
  const PointLL* p0 = &pts[0];
  const PointLL* p1 = &pts[1];
  const PointLL* end = &pts[count - 1];
  for (int index = 0; p1 < end; index++, p0++, p1++) {
    // Construct vector v1 - represents the segment.  Skip 0 length segments
    v1.Set(*p0, *p1);
    if (v1.x() == 0.0f && v1.y() == 0.0f)
      continue;

    // Vector v2 from the segment origin to the target point
    v2.Set(*p0, *this);

    // Find the dot product of v1 and v2.  If less than 0 the segment
    // origin is the closest point.  Find the distance and continue
    // to the next segment.
    dot = v1.Dot(v2);
    if (dot <= 0.0f) {
      beyond_end = false;
      dist = approx.DistanceSquared(*p0);
      if (dist < mindist) {
        mindist = dist;
        closest = *p0;
        idx = index;
      }
      continue;
    }

    // Closest point is either beyond the end of the segment or at a point
    // along the segment. Find the component of v2 along v1
    comp = dot / v1.Dot(v1);

    // If component >= 1.0 the segment end is the closest point. A future
    // polyline segment will be closer.  If last segment we need to check
    // distance to the endpoint.  Set flag so this happens.
    if (comp >= 1.0f)
      beyond_end = true;
    else {
      // Closest point is along the segment.  The closest point is found
      // by adding the projection of v2 onto v1 to the origin point.
      // The squared distance from this point to the target is then found.
      // TODO - why not:  *p0 + v1 * comp;
      beyond_end = false;
      projpt.Set(p0->lng() + v1.x() * comp, p0->lat() + v1.y() * comp);
      dist = approx.DistanceSquared(projpt);
      if (dist < mindist) {
        mindist = dist;
        closest = projpt;
        idx = index;
      }
    }
  }

  // Test the end point if flag is set - it may be the closest point
  if (beyond_end) {
    dist = approx.DistanceSquared(pts[count - 1]);
    if (dist < mindist) {
      mindist = dist;
      closest = *end;
      idx = (int) (count - 2);
    }
  }
  return mindist;
}

// Calculate the heading from the start of a polyline of lat,lng points to a
// point the specified distance from the start.
float PointLL::HeadingAlongPolyline(const std::vector<PointLL>& pts,
                           const float dist) {
  int n = (int)pts.size();
  if (n < 2) {
    // TODO error!?
    return 0.0f;
  }
  if (n == 2) {
    return pts[0].Heading(pts[1]);
  }

  int i = 0;
  double d = 0.0;
  double seglength = 0.0;
  while (d < dist && i < n-1) {
    seglength = pts[i].Distance(pts[i+1]);
    if (d + seglength > dist) {
      // Set the extrapolated point along the line.
      float pct = (float)((dist - d) / seglength);
      PointLL ll(pts[i].lng() + ((pts[i+1].lng() - pts[i].lng()) * pct),
                 pts[i].lat() + ((pts[i+1].lat() - pts[i].lat()) * pct)
                );
      return pts[0].Heading(ll);
    } else {
      d += seglength;
      i++;
    }
  }

  // Length of polyline must have exceeded the distance. Return heading from
  // first to last point.
  return pts[0].Heading(pts[1]);
}

// Calculate the heading from the end of a polyline of lat,lng points to a
// point the specified distance from the end.
float PointLL::HeadingAtEndOfPolyline(const std::vector<PointLL>& pts,
                        const float dist) {
  int n = (int)pts.size();
  if (n < 2) {
    // TODO error!?
    return 0.0f;
  }
  if (n == 2) {
    return pts[0].Heading(pts[1]);
  }

  float heading = 0.0f;
  int i = n - 2;
  double d = 0.0;
  double seglength;
  while (d < dist && i >= 0) {
    seglength = pts[i].Distance(pts[i+1]);
    if (d + seglength > dist) {
      // Set the extrapolated point along the line.
      float pct = (float)((dist - d) / seglength);
      PointLL ll(pts[i+1].lng() + ((pts[i].lng() - pts[i+1].lng()) * pct),
                 pts[i+1].lat() + ((pts[i].lat() - pts[i+1].lat()) * pct)
                );
      return ll.Heading(pts[n-1]);
    } else {
      d += seglength;
      i--;
    }
  }

  // Length of polyline must have exceeded the distance. Return heading from
  // first to last point.
  return pts[0].Heading(pts[1]);
}

}
}
