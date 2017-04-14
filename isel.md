Given:

  $div = i4(i4 $a, i4 $b)
  {
    $d = udiv $a, $b
    $e = udiv $b, i4 2

    $s = add $d, $e
    ret $s
  }

in isel, we can't allocate registers to $a and $b directly, since they both need to be $eax for the div. This gives us a contradiction in that at the first udiv, $b is %eax, but as is $a, overlapping use of the register.
Instead we have to do the following (optimally we can avoid this unless we detect reg-overlap):

  $div = i4(i4 $a, i4 $b)
  {
    $div.1 = $a
    $div.2 = $b
    $d = udiv $div.1, $div.2

    $div.3 = $b
    $e = udiv $div.3, i4 2

    $s = add $d, $e
    ret $s
  }

This allows us to freely assign $div.[13] to %eax without breaking other div constraints, and leave regalloc to cleanup the mess.
