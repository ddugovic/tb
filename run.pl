#!/usr/bin/perl

$ENV{'STBWDIR'} = '.';
$ENV{'STBZDIR'} = '.';
$ENV{'STBSTATSDIR'} = '.';
$ENV{'LTBWDIR'} = '.';
$ENV{'LTBZDIR'} = '.';
$ENV{'LTBSTATSDIR'} = '.';
$ENV{'GTBWDIR'} = '.';
$ENV{'GTBZDIR'} = '.';
$ENV{'GTBSTATSDIR'} = '.';
$ENV{'ATBWDIR'} = '.';
$ENV{'ATBZDIR'} = '.';
$ENV{'ATBSTATSDIR'} = '.';
$ENV{'RTBWDIR'} = '.';
$ENV{'RTBZDIR'} = '.';
$ENV{'RTBSTATSDIR'} = '.';

use Getopt::Long;

$threads = 4;
$generate = '';
$verify = '';
$wdl = '';
$pieces = 'QRBNP';
$min = 3;
$max = 5;
$disk = '';
GetOptions('threads=i' => \$threads,
	   'generate' => \$generate,
	   'verify' => \$verify,
	   'wdl' => \$wdl,
	   'pieces=s' => \$pieces,
	   'min=i' => \$min,
	   'max=i' => \$max,
	   'disk' => \$disk);

sub Execute {
    my $cmd = shift;
    print "$cmd\n";
    die if system($cmd);
}

sub Process {
  my($tb) = @_;
  my $len = length($tb) - 1;
  if ($len < $min || $len > $max) { return; }
  $wopt = "";
  if ($wdl) {
    $wopt = "-w ";
  }
  $dopt = "";
  if ($disk && $len == 6) {
    $dopt = "-d ";
  }
  if ($generate && !-e $tb.".rtbz") {
    print "\nGenerating $tb\n";
    if ($tb !~ /.*P.*/) {
      Execute("src/rtbgen $wopt$dopt-t $threads --stats $tb");
    } else {
      Execute("src/rtbgenp $wopt$dopt-t $threads --stats $tb");
    }
  }
  if ($generate && !-e $tb.".atbz") {
    print "\nGenerating $tb\n";
    if ($tb !~ /.*P.*/) {
      Execute("src/atbgen $wopt$dopt-t $threads --stats $tb");
    } else {
      Execute("src/atbgenp $wopt$dopt-t $threads --stats $tb");
    }
  }
  if ($verify) {
    printf "\nVerifying $tb\n";
    if ($tb !~ /.*P.*/) {
      Execute("src/atbver $wopt-t $threads --log $tb");
      Execute("src/rtbver $wopt-t $threads --log $tb");
    } else {
      Execute("src/atbverp $wopt-t $threads --log $tb");
      Execute("src/rtbverp $wopt-t $threads --log $tb");
    }
  }
}

@Pieces = unpack '(A)*', $pieces;
$N = scalar (@Pieces);

for ($i = 0; $i < $N; ++$i) {
  $a = @Pieces[$i];
  $tb = "K".$a."v"."K";
  Process($tb);
}

for ($i = 0; $i < $N; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < $N; ++$j) {
    $b = @Pieces[$j];
    $tb = "K".$a.$b."v"."K";
    Process($tb);
  }
}

for ($i = 0; $i < $N; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < $N; ++$j) {
    $b = @Pieces[$j];
    $tb = "K".$a."v"."K".$b;
    Process($tb);
  }
}

for ($i = 0; $i < $N; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < $N; ++$j) {
    $b = @Pieces[$j];
    for ($k = $j; $k < $N; ++$k) {
      $c = @Pieces[$k];
      $tb = "K".$a.$b.$c."v"."K";
      Process($tb);
    }
  }
}

for ($i = 0; $i < $N; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < $N; ++$j) {
    $b = @Pieces[$j];
    for ($k = 0; $k < $N; ++$k) {
      $c = @Pieces[$k];
      $tb = "K".$a.$b."v"."K".$c;
      Process($tb);
    }
  }
}

for ($i = 0; $i < $N; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < $N; ++$j) {
    $b = @Pieces[$j];
    for ($k = $j; $k < $N; ++$k) {
      $c = @Pieces[$k];
      for ($l = $k; $l < $N; ++$l) {
	$d = @Pieces[$l];
	$tb = "K".$a.$b.$c.$d."v"."K";
	Process($tb);
      }
    }
  }
}

for ($i = 0; $i < $N; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < $N; ++$j) {
    $b = @Pieces[$j];
    for ($k = $j; $k < $N; ++$k) {
      $c = @Pieces[$k];
      for ($l = 0; $l < $N; ++$l) {
	$d = @Pieces[$l];
	$tb = "K".$a.$b.$c."v"."K".$d;
	Process($tb);
      }
    }
  }
}

for ($i = 0; $i < $N; ++$i) {
  $a = @Pieces[$i];
  for ($j = $i; $j < $N; ++$j) {
    $b = @Pieces[$j];
    for ($k = $i; $k < $N; ++$k) {
      $c = @Pieces[$k];
      for ($l = ($i == $k) ? $j : $k; $l < $N; ++$l) {
	$d = @Pieces[$l];
	$tb = "K".$a.$b."v"."K".$c.$d;
	Process($tb);
      }
    }
  }
}

