//%attributes = {"invisible":true,"preemptive":"capable"}
var $h; $h2; $bcrypt; $pw : Text


// hash then verify
$h:=Argon2 Generate password hash("correct horse")
ASSERT(Position("$argon2id$v=19$m=19456,t=2,p=1$"; $h)=1)
ASSERT(Argon2 Verify password hash("correct horse"; $h))

// wrong password
ASSERT(Not(Argon2 Verify password hash("wrong horse"; $h)))
ASSERT(Not(Argon2 Verify password hash(""; $h)))

// garbage, bcrypt and non-argon2id hashes do not throw and return False
ASSERT(Not(Argon2 Verify password hash("pw"; "")))
ASSERT(Not(Argon2 Verify password hash("pw"; "garbage")))
ASSERT(Not(Argon2 Verify password hash("pw"; "$argon2id$")))
ASSERT(Not(Argon2 Verify password hash("pw"; "$argon2id$v=19$m=8,t=1,p=1$AAAA$BBBB")))
ASSERT(Not(Argon2 Verify password hash("pw"; "日本語")))
ASSERT(Not(Argon2 Verify password hash("pw"; "$argon2i$v=19$m=8192,t=1,p=1$c29tZXNhbHRzb21lc2FsdA$aGFzaGhhc2hoYXNoaGFzaGhhc2hoYXNoaGFzaGhhc2g")))
$bcrypt:=Generate password hash("pw")
ASSERT(Not(Argon2 Verify password hash("pw"; $bcrypt)))

// random salt
$h2:=Argon2 Generate password hash("correct horse")
ASSERT($h#$h2)
ASSERT(Argon2 Verify password hash("correct horse"; $h2))

// custom parameters appear in the PHC string
$h:=Argon2 Generate password hash("pw"; New object("memory"; 8192; "iterations"; 3))
ASSERT(Position("$argon2id$v=19$m=8192,t=3,p=1$"; $h)=1)
ASSERT(Argon2 Verify password hash("pw"; $h))
$h:=Argon2 Generate password hash("pw"; New object("iterations"; 1))
ASSERT(Position("m=19456,t=1,p=1"; $h)>0)
$h:=Argon2 Generate password hash("pw"; New object)
ASSERT(Position("m=19456,t=2,p=1"; $h)>0)

// needs rehash
$h:=Argon2 Generate password hash("pw")
ASSERT(Not(Argon2 Hash needs rehash($h)))
ASSERT(Not(Argon2 Hash needs rehash($h; New object("memory"; 19456; "iterations"; 2))))
ASSERT(Argon2 Hash needs rehash($h; New object("memory"; 32768)))
ASSERT(Argon2 Hash needs rehash($h; New object("iterations"; 3)))
$h:=Argon2 Generate password hash("pw"; New object("memory"; 8192; "iterations"; 3))
ASSERT(Not(Argon2 Hash needs rehash($h; New object("memory"; 8192; "iterations"; 3))))
ASSERT(Argon2 Hash needs rehash($h))
ASSERT(Argon2 Hash needs rehash("garbage"))
ASSERT(Argon2 Hash needs rehash(""))
ASSERT(Argon2 Hash needs rehash($bcrypt))

// empty password
$h:=Argon2 Generate password hash("")
ASSERT(Argon2 Verify password hash(""; $h))
ASSERT(Not(Argon2 Verify password hash("x"; $h)))

// 1024 bytes accepted
$pw:="a"*1024
$h:=Argon2 Generate password hash($pw)
ASSERT(Argon2 Verify password hash($pw; $h))
ASSERT(Not(Argon2 Verify password hash("a"*1023; $h)))
$h:=Argon2 Generate password hash("日"*341)  // 1023 bytes
ASSERT(Argon2 Verify password hash("日"*341; $h))

// non-ASCII passwords
$h:=Argon2 Generate password hash("pässwörd")
ASSERT(Argon2 Verify password hash("pässwörd"; $h))
ASSERT(Not(Argon2 Verify password hash("passwörd"; $h)))
$h:=Argon2 Generate password hash("パスワード🔑")
ASSERT(Argon2 Verify password hash("パスワード🔑"; $h))
ASSERT(Not(Argon2 Verify password hash("パスワード"; $h)))
