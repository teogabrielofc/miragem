' Miragem ZIP app sample - entrypoint shims.
'
' Serve dois tipos de slot SDK:
'   - canal comum: o launcher chama Main(args);
'   - canal SGDEX (como o 2213): o RunUserInterface do SGDEX chama
'     GetSceneName() e depois MainInit(screen, args).
' O push (tools/zipapp.sh) sobrepoe este source/main.brs no pacote original,
' mantendo o manifest (token/permissoes) do slot.

sub Main(args as Object)
    ZipAppRun(args)
end sub

function GetSceneName() as String
    return "ZipAppScene"
end function

sub MainInit(screen as Object, args as Object)
    ZipAppProve()
end sub

sub MainHandleEvent(msg as Object)
    ' sem eventos especiais
end sub
