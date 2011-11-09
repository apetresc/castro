#!/usr/bin/ruby


$infile = nil  # read an mminput file
$outfile = nil # output the mapped file to here
$mapfile = nil # output the mapping to here



while(ARGV.length > 0)
	arg = ARGV.shift
	case arg
	when "-i", "--infile"   then $infile        = ARGV.shift
	when "-o", "--outfile"  then $outfile       = ARGV.shift
	when "-m", "--mapfile"  then $mapfile       = ARGV.shift
	when "-h", "--help"     then
		puts "Map an mm output file back to the patterns and their gammas"
		puts "Usage: #{$0} -i infile -o outfile -m mapfile"
		puts "  -i --infile   Input file, generated by mm"
		puts "  -o --outfile  Output file, with the patterns back"
		puts "  -m --mapfile  Input mapping between patterns and pattern number"
		puts "  -h --help     Print this help"
		exit;
	else
		puts "Unknown argument #{arg}"
		exit
	end
end

if !$infile || !$outfile || !$mapfile
	puts "Missing arguments, -h for help"
	exit
end

map = {};

File.open($mapfile){|infile|
	infile.each_line {|line|
		line = line.strip.split
		map[line[0]] = line[1]
	}
}

map['0'] = map.size

File.open($outfile, 'w'){|outfile|
	File.open($infile){|infile|
		infile.each_line {|line|
			line = line.strip.split
			outfile.puts "#{map[line[0]]} #{line[1]}"
		}
	}
}
