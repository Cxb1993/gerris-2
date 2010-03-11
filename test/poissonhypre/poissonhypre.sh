if  ! $donotrun; then
    rm -f error
    echo "0 0.00000000" > time
    echo "0 1.776e+02 0" > proj
    for cycle in 1 2 3 4 5 6 7 8 9 10 ; do
	if ( gerris2D -m -DLEVEL=7 -DCYCLE=$cycle $1 ) ; then :
	else
	    exit 1
	fi     
    done
    join time proj | awk '{if (NR == 1) {print $0; old = $3} else {print $1" "$2" "$3" "old/$3; old=$3}}' > res-7
    rm -f error order proj time runtime status
    for level in 3 4 5 6 7 8; do
	if ( gerris2D -m -DLEVEL=$level -DCYCLE=10 $1 ) ; then :
	else
	    exit 1
	fi
    done
fi

if awk '
BEGIN { n = 0 }
{
  l[n] = $1; n1[n] = $2; n2[n] = $3; ni[n++] = $4;
}
END {
  for (i = 1; i < n; i++)
    print l[i] " " log(n1[i-1]/n1[i])/log(2.) " " log(n2[i-1]/n2[i])/log(2.) " " log(ni[i-1]/ni[i])/log(2.);
}' < error > order; then :
else
    exit 1
fi

if cat <<EOF | gnuplot ; then :
    set term postscript eps color lw 3 solid 20
    set output 'residual.eps'
    set xlabel 'CPU time'
    set ylabel 'Maximum residual'
    set logscale y
    plot 'res-7.ref' u 2:3 t 'Gerris' w lp, 'res-7' u 2:3 t 'Hypre' w lp
    set output 'rate.eps'
    set xlabel 'V-cycle'
    set ylabel 'Cumulative residual reduction factor'
    unset logscale
    plot 'res-7.ref' u 1:4 t 'Gerris' w lp, 'res-7' u 1:4 t 'Hypre' w lp
    set output 'error.eps'
    set xlabel 'Level'
    set ylabel 'Error norms'
    set key
    set logscale y
    plot 'error.ref' u 1:2 t '1 (ref)' w lp, \
         'error.ref' u 1:3 t '2 (ref)' w lp, \
         'error.ref' u 1:4 t 'max (ref)' w lp, \
         'error' u 1:2 t '1' w lp, \
         'error' u 1:3 t '2' w lp, \
         'error' u 1:4 t 'max' w lp
    set output 'order.eps'
    set xlabel 'Level'
    set ylabel 'Order'
    set key
    unset logscale
    set xtics 0,1
    set ytics 0,1
    set grid
    plot [][0:3] 'order.ref' u 1:2 t '1 (ref)' w lp, \
                 'order.ref' u 1:3 t '2 (ref)' w lp, \
                 'order.ref' u 1:4 t 'max (ref)' w lp, \
                 'order' u 1:2 t '1' w lp, \
                 'order' u 1:3 t '2' w lp, \
                 'order' u 1:4 t 'max' w lp
EOF
else
    exit 1
fi

if cat <<EOF | python ; then :
from check import *
from sys import *
c = Curve()
if (Curve('res-7',1,3) - Curve('res-7-hypre.ref',1,3)).max() > 1e-8 or\
   (Curve('error',1,4) - Curve('error.ref',1,4)).max() > 1e-6:
    exit(1)
EOF
else
   exit 1
fi