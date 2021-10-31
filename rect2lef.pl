#!/usr/bin/perl

if ($#ARGV != 1) {
	die "Usage: $0 <file> <name>";
	exit 1;
}

$name = $ARGV[0];
$outname = $ARGV[1];
$scale = 0.001;

die "Could not open .rect file for $name" unless open (FILE,$name . ".rect");

$found = 0;
while (<FILE>) {
  if (/^bbox/) {
	@box = split;
	$found = 1;
  }
}
close (FILE);

die "Could not find bbox line" unless $found == 1;

$xsize = $box[3] - $box[1];
$ysize = $box[4] - $box[2];

print "MACRO $outname\n";
print "   CLASS CORE ;\n";
print "   FOREIGN $outname 0.000000 0.000000 ;\n";
print "   ORIGIN 0.000000 0.000000 ; \n";
printf("   SIZE %.6f BY %.6f ; \n", $xsize * $scale, $ysize * $scale);
print "   SYMMETRY X Y ;\n";
print "   SITE CoreSite ;\n";
print "END $outname\n\n";
