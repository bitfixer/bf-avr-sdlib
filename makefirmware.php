<?php
/*
    makefirmware.php
    Convert intel hex to raw binary firmware for the PETdisk storage device
    Copyright (C) 2011 Michael Hill

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    Contact the author at bitfixer@bitfixer.com
    http://bitfixer.com
*/

// process intel hex file into my firmware format

if (count($argv) < 4)
{
    print "usage: php makefirmware.php <input_hexfile> <output_binfile> <programsize>";
    die;
}

$infile = $argv[1];
$outfile = $argv[2];
$programsize = $argv[3];

$contents = file_get_contents($infile);
$fpout = fopen($outfile, "wb");

$lines = explode(':', $contents);

$totalbytes = 0;
$prevaddr = 0;
foreach ($lines as $l)
{
    $strlen = strlen($l);
    if ($strlen > 2)
    {
        $numchars = substr($l, 0, 2);
        //print "$numchars chars\n";
        $chars = hexdec($numchars);
        print "$chars dec\n";
        
        $hexaddr = substr($l, 2, 4);
        $address = hexdec($hexaddr);
        print "addr $address $hexaddr prev $prevaddr\n";
        
        if ($address > $prevaddr)
        {
            print "****\n";
            
            // add empty space to fill in
            while ($address > $prevaddr)
            {   
                $totalbytes++;
                $thisdec = 0xff;
                $bin = pack('C', $thisdec);
                fwrite($fpout, $bin);
                
                if ($totalbytes >= $programsize)
                {
                    print "output $programsize bytes.";
                    fclose($fpout);
                    die;
                }
                
                $prevaddr++;
            }
            
        }
        
        $prevaddr = $address + $chars;
        
        $type = substr($l, 6, 2);
        print "type $type\n";
        
        
        for ($c = 0; $c < $chars; $c++)
        {
            $thischar = substr($l, 8+($c*2), 2);
            //print "$thischar ";
            $totalbytes++;
            $thisdec = hexdec($thischar);
            $bin = pack('C', $thisdec);
            fwrite($fpout, $bin);
            
            if ($totalbytes >= $programsize)
            {
                print "output $programsize bytes.";
                fclose($fpout);
                die;
            }
        }
        
        print "\n";
    }
}

print "$totalbytes bytes.\n";

$rem = $programsize - $totalbytes;
print "remaining $rem bytes.\n";

for ($i = 0; $i < $rem; $i++)
{
    $bin = pack('C', 0xff);
    fwrite($fpout, $bin);
}

fclose($fpout);

?>