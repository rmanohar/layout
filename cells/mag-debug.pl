#!/usr/bin/perl

$count = 0;
$start = 1;

while (<>) {
    if (/^#/) {
       if ($start == 0) {
          print "}\n";
      }
      $start = 1;
      next;
    }
    chop;
    if ($start) {
      print "proc p_${count} {} {\n"; 
      $count++;
      $start = 0;
    }
    ($rect,$node,$mat,$llx,$lly,$urx,$ury) = split;
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
if ($start == 0) {
   print "}\n";
}
