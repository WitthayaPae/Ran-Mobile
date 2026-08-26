/\*\*\* Dumping AST Record Layout/ { want=1; name=""; next }
want && /\| (struct|class|union) / {
  n=$0; sub(/.*(struct|class|union) /,"",n); sub(/ .*/,"",n); name=n; want=0; next
}
/\[sizeof=/ {
  if (name!="") { s=$0; sub(/.*\[sizeof=/,"",s); sub(/,.*/,"",s); print name, s; name="" }
}
