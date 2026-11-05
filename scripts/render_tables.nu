def show_section [section, width] {
  print $"\n($section.title)"
  print ($section.results | table --width $width)
  print "\nUserspace statistics (getrusage)"
  print ($section.performance | table --width $width)
}

def main [] {
  let report = $in | from json
  let requested_width = $env.TABLE_WIDTH? | default $env.COLUMNS? | default 120 | into int
  let width = if $requested_width > 0 { $requested_width } else { 120 }

  for section in $report.sections {
    show_section $section $width
  }

  let summary = $report | get -o summary
  if $summary != null {
    print "\nSummary"
    print ($summary | table --width $width)
  }
}
