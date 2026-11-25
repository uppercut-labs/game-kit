# An SDL gamepad: SDL's standard controls, named by position. SDL's own names ("a", "dpup", "leftx") follow the
# control name; pressed values are raw axis values, a trigger's rest is its minimum.
control south gbutton a
control east gbutton b
control west gbutton x
control north gbutton y
control leftshoulder gbutton leftshoulder
control rightshoulder gbutton rightshoulder
control lefttrigger gaxis lefttrigger 32767 -32768
control righttrigger gaxis righttrigger 32767 -32768
control back gbutton back
control start gbutton start
control dpup gbutton dpup
control dpright gbutton dpright
control dpdown gbutton dpdown
control dpleft gbutton dpleft
control leftstick-up gaxis lefty -32767 0
control leftstick-right gaxis leftx 32767 0
control leftstick-down gaxis lefty 32767 0
control leftstick-left gaxis leftx -32767 0
control leftstickclick gbutton leftstick
control rightstickclick gbutton rightstick
