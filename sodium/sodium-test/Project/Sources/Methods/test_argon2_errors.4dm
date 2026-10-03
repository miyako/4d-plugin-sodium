//%attributes = {"invisible":true,"preemptive":"capable"}
#DECLARE()->$ok : Boolean
var $h; $h2 : Text
var $failed : Boolean
var $err : Collection

$ok:=True

ON ERR CALL("test_err_handler")

$h:=Argon2 Generate password hash("pw")

/*
	Errors are raised with the built-in throw command from inside the plugin.
	They are pushed on the 4D error stack (Last errors) and the command returns
	an empty text / False. Without an ON ERR CALL handler 4D aborts the calling
	method; with one installed execution continues.
	Each check is preceded by a different error code than the previous one.
	ASSERT is not used: with ON ERR CALL installed a failed assertion is swallowed.
*/

// 1025 bytes rejected (never truncated): code 2
$h2:=Argon2 Generate password hash("a"*1025)
$ok:=$ok & ($h2="")
$err:=Last errors.query("componentSignature = :1 & errCode = :2"; "sodm"; 2)
$ok:=$ok & ($err.length>0)

// out-of-range memory: code 7
$h2:=Argon2 Generate password hash("pw"; New object("memory"; 1))
$ok:=$ok & ($h2="")
$err:=Last errors.query("componentSignature = :1 & errCode = :2"; "sodm"; 7)
$ok:=$ok & ($err.length>0)

// verify also rejects over-long passwords: code 2
$failed:=Argon2 Verify password hash("a"*1025; $h)
$ok:=$ok & (Not($failed))
$err:=Last errors.query("componentSignature = :1 & errCode = :2"; "sodm"; 2)
$ok:=$ok & ($err.length>0)

// unknown option (parallelism is fixed by libsodium): code 5
$h2:=Argon2 Generate password hash("pw"; New object("parallelism"; 2))
$ok:=$ok & ($h2="")
$err:=Last errors.query("componentSignature = :1 & errCode = :2"; "sodm"; 5)
$ok:=$ok & ($err.length>0)

// multi-byte characters count in UTF-8 bytes (342 x 3 = 1026): code 2
$h2:=Argon2 Generate password hash("日"*342)
$ok:=$ok & ($h2="")
$err:=Last errors.query("componentSignature = :1 & errCode = :2"; "sodm"; 2)
$ok:=$ok & ($err.length>0)

// wrong option type: code 6
$h2:=Argon2 Generate password hash("pw"; New object("iterations"; "2"))
$ok:=$ok & ($h2="")
$err:=Last errors.query("componentSignature = :1 & errCode = :2"; "sodm"; 6)
$ok:=$ok & ($err.length>0)

// zero iterations: code 7
$h2:=Argon2 Generate password hash("pw"; New object("iterations"; 0))
$ok:=$ok & ($h2="")
$err:=Last errors.query("componentSignature = :1 & errCode = :2"; "sodm"; 7)
$ok:=$ok & ($err.length>0)

// unknown option on needs-rehash: code 5
$failed:=Argon2 Hash needs rehash("x"; New object("bogus"; 1))
$ok:=$ok & (Not($failed))
$err:=Last errors.query("componentSignature = :1 & errCode = :2"; "sodm"; 5)
$ok:=$ok & ($err.length>0)

// non-integer memory: code 7
$h2:=Argon2 Generate password hash("pw"; New object("memory"; 1.5))
$ok:=$ok & ($h2="")
$err:=Last errors.query("componentSignature = :1 & errCode = :2"; "sodm"; 7)
$ok:=$ok & ($err.length>0)

ON ERR CALL("")
