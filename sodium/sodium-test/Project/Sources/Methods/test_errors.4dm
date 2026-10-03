//%attributes = {"invisible":true}
If (Application info.headless)
	
	If (test_argon2_errors)
		LOG EVENT(Into system standard outputs; "PASS"; Information message)
	End if 
	
End if 
