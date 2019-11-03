#!/usr/bin/perl


foreach $i (@ARGV) {
    &process_rect ($i);
}

exit 0;

sub process_rect {
    my $name = $_[0];
    my $base;

    die "Could not open file $name\n" unless open(FILE,"<$name");

    $base = $name;
    $base =~ s/.rect$//;

    print "load $base\n";

    while (<FILE>) {
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
    close (FILE);
}
