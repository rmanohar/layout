#!/usr/bin/perl

while (<>) {
  chop;
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
    print "label \"$node\"\n";
  }
}
