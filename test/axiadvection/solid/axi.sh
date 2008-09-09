if ! $donotrun; then
    gerris2D solid.gfs
fi

if awk '
BEGIN { min = 1000.; max = -1000.; }{ 
  if ($5 < min) min = $5; 
  if ($5 > max) max = $5; 
}
END {
  e = 2.*(max - min)/(max + min);
  print "VOF:", e;
  if (e > 2e-2)
    exit (1);
}' < srt; then :
else
    exit 1
fi

if awk '
BEGIN { min = 1000.; max = -1000.; }{ 
  if ($5 < min) min = $5; 
  if ($5 > max) max = $5; 
}
END {
  e = 2.*(max - min)/(max + min);
  print "Standard:", e;
  if (e > 3e-5)
    exit (1);
}' < srt1; then :
else
    exit 1
fi