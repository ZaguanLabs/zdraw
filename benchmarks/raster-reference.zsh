# Captured z3dfx scanline reference, 2026-09-14. Kept unchanged for comparison.
# Receives x,y,inverse-depth triples and a material; globals belong to the driver.
raster-reference() {
  local -F 8 ax=$1 ay=$2 az=$3 bx=$4 by=$5 bz=$6 cx=$7 cy=$8 cz=$9
  local -i material=${10} minx maxx miny maxy x y idx left right edge
  local -F 8 area wa wb wc dax day dbx dby dcx dcy rowa rowb rowc invz dz bound lo hi edgea edgeb edgec
  local -a starts slopes
  (( area=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax) ))
  (( abs(area)>0.000001 )) || return 0
  (( minx=int(floor(ax<bx ? (ax<cx ? ax:cx) : (bx<cx ? bx:cx))),
     maxx=int(ceil(ax>bx ? (ax>cx ? ax:cx) : (bx>cx ? bx:cx))),
     miny=int(floor(ay<by ? (ay<cy ? ay:cy) : (by<cy ? by:cy))),
     maxy=int(ceil(ay>by ? (ay>cy ? ay:cy) : (by>cy ? by:cy))),
     minx=minx<0?0:minx, maxx=maxx>=width?width-1:maxx,
     miny=miny<0?0:miny, maxy=maxy>=height?height-1:maxy ))
  (( minx<=maxx && miny<=maxy )) || return 0
  (( dax=(by-cy)/area, day=(cx-bx)/area,
     dbx=(cy-ay)/area, dby=(ax-cx)/area, dcx=-dax-dbx, dcy=-day-dby,
     rowa=((bx-minx-0.5)*(cy-miny-0.5)-(by-miny-0.5)*(cx-minx-0.5))/area,
     rowb=((cx-minx-0.5)*(ay-miny-0.5)-(cy-miny-0.5)*(ax-minx-0.5))/area,
     rowc=1-rowa-rowb, triangles_drawn++,
     edgea=0.7*sqrt(dax*dax+day*day), edgeb=0.7*sqrt(dbx*dbx+dby*dby),
     edgec=0.7*sqrt(dcx*dcx+dcy*dcy) ))
  for ((y=miny;y<=maxy;y++)); do
    # Intersect three barycentric half-planes with this scanline. Only covered
    # pixels enter the expensive shell loop, including near-clipped triangles.
    ((lo=0, hi=maxx-minx))
    starts=($rowa $rowb $rowc) slopes=($dax $dbx $dcx)
    for edge in 1 2 3; do
      if ((abs(slopes[edge])<0.000000001)); then
        ((starts[edge]<-0.000001)) && hi=-1
      else
        ((bound=(-0.000001-starts[edge])/slopes[edge]))
        if ((slopes[edge]>0)); then ((lo=bound>lo?bound:lo))
        else ((hi=bound<hi?bound:hi)); fi
      fi
    done
    ((left=minx+int(ceil(lo)), right=minx+int(floor(hi)),
      wa=rowa+(left-minx)*dax, wb=rowb+(left-minx)*dbx, wc=1-wa-wb,
      invz=wa*az+wb*bz+wc*cz, dz=dax*az+dbx*bz+dcx*cz,
      idx=y*width+left+1))
    for ((x=left;x<=right;x++)); do
      ((invz>depth[idx])) && ((depth[idx]=invz,
        pixels[idx]=(!wireframe || wa<edgea || wb<edgeb || wc<edgec)?material:1,
        fragments++))
      ((invz+=dz, wa+=dax, wb+=dbx, wc+=dcx, idx++))
    done
    (( rowa+=day, rowb+=dby, rowc+=dcy ))
  done
  return 0
}
