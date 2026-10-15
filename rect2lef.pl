#!/usr/bin/perl

if ($#ARGV != 2 && $#ARGV != 3) {
	die "Usage: $0 <file> <name> <micron_conversion> [CORE|COVER]";
	exit 1;
}

$name = $ARGV[0];
$outname = $ARGV[1];
$scale = 1.0/$ARGV[2];
$class = "CORE";

if ($#ARGV == 3) {
    $class = $ARGV[3];
}
if ($class ne "CORE" && $class ne "COVER") {
    die "Invalid macro class '$class': expected CORE or COVER";
}


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
print "   CLASS $class ;\n";
print "   FOREIGN $outname 0.000000 0.000000 ;\n";
print "   ORIGIN 0.000000 0.000000 ; \n";
printf("   SIZE %.6f BY %.6f ; \n", $xsize * $scale, $ysize * $scale);
print "   SYMMETRY X Y ;\n";
print "   SITE CoreSite ;\n";
print "END $outname\n\n";
