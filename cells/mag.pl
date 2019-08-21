#!/usr/bin/perl

while (<>) {
    next if /^#/;
    chop;
    ($rect,$node,$mat,$llx,$lly,$urx,$ury) = split;
    next if ($rect ne "rect");
    print "box $llx $lly $urx $ury\n";
    print "paint $mat\n";
    if ($node ne "#") {
	$llx++;
	if ($lly < $ury) {
	    $lly++;
	}
	else { 
	    $lly--;
	}
	print "box $llx $lly $llx $lly\n";
	$node =~ s/\[/\(/g;
	$node =~ s/\]/\)/g;
	print "label \"$node\"\n";
    }
}
