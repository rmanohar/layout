#!/usr/bin/perl

if ($ARGV[0] eq "-prboundary") {
    $prboundary = 1;
    shift;
}
else {
    $prboundary = 0;
}

foreach $i (@ARGV) {
    &process_rect ($i);
}
&std_defs ();

exit 0;

sub std_defs {
    print 'proc lcell { x } { load "_0_0cell_0_0g${x}x0" }' . "\n";
}


sub process_rect {
    my $name = $_[0];
    my $base;

    die "Could not open file $name\n" unless open(FILE,"<$name");

    $base = $name;
    $base =~ s/.rect$//;

    print "xload $base\n";

    while (<FILE>) {
	next if /^#/;
	chop;
	($rect,$node,$mat,$llx,$lly,$urx,$ury) = split;
	if ($rect eq "bbox" || $rect eq "sbox") {
	    if ($prboundary) {
		print "box $node $mat $llx $lly\n";
		print "label prboundary\n";
	    }
	    next;
        }
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
	    print "label \"$node\" right $mat\n";
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
