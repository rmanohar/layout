#!/usr/bin/perl

while (<>) {
    next if /^#/;
    chop;
    ($rect,$node,$mat,$llx,$lly,$urx,$ury) = split;
    if ($llx > $urx) {
	$tmp = $llx;
	$llx = $urx;
	$urx = $tmp;
    }
    if ($lly > $ury) {
	$tmp = $lly;
	$lly = $ury;
	$ury = $tmp;
    }
    next if ($rect ne "rect") && ($rect ne "inrect") && ($rect ne "outrect");
    print "box $llx $lly $urx $ury\n";
    print "paint $mat\n";
    if ($node ne "#") {
	$llx++;
	$lly++;
	print "box $llx $lly $llx $lly\n";
	$node =~ s/\[/\(/g;
	$node =~ s/\]/\)/g;
	print "label \"$node\"\n";
	$llx--;
	$lly--;
	if ($rect eq "inrect") {
	    print "port class input\n";
	    print "port make\n";
	}
	elsif ($rect eq "outrect") {
	    print "port class output\n";
	    print "port make\n";
	}
    }
}
