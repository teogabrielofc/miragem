' Miragem ZIP app sample - prova de execucao.
'
' Os placeholders __ZIPAPP_MARKER__ e __ZIPAPP_BEACON__ sao trocados pelo
' tools/zipapp.sh no push. O beacon e' opcional (vira "" se nao for passado).
'
' A prova tem tres partes:
'   1. print no console BrightScript (visivel no log do SdkLauncher);
'   2. tmp:/zipapp_proof.txt escrito pelo proprio app (uid 501);
'   3. GET HTTP para o PC (roUrlTransfer), se um beacon foi configurado.

sub ZipAppProve()
    marker = "__ZIPAPP_MARKER__"
    beacon = "__ZIPAPP_BEACON__"
    steps = "start"

    print "ZIPAPP_PROOF: "; marker

    ok = WriteAsciiFile("tmp:/zipapp_proof.txt", marker)
    if ok then
        steps = steps + ";proof=true"
    else
        steps = steps + ";proof=false"
    end if
    WriteAsciiFile("tmp:/zipapp_steps.txt", steps)
    print "ZIPAPP_PROOF_FILE: "; ok

    if beacon <> "" then
        steps = steps + ";url=" + beacon
        WriteAsciiFile("tmp:/zipapp_steps.txt", steps)
        port = CreateObject("roMessagePort")
        http = CreateObject("roUrlTransfer")
        http.SetPort(port)
        http.SetUrl(beacon)
        steps = steps + ";set"
        WriteAsciiFile("tmp:/zipapp_steps.txt", steps)
        ' Sincrono de proposito: o objeto local seria coletado antes do envio
        ' assincrono terminar. O beacon e' opcional e o PC responde rapido.
        result = http.GetToString()
        steps = steps + ";sync_bytes=" + Str(Len(result))
        WriteAsciiFile("tmp:/zipapp_steps.txt", steps)
        print "ZIPAPP_BEACON: "; Len(result); " "; beacon
    else
        WriteAsciiFile("tmp:/zipapp_steps.txt", steps + ";no-beacon")
    end if
end sub

sub ZipAppRun(args as Object)
    ZipAppProve()

    screen = CreateObject("roSGScreen")
    port = CreateObject("roMessagePort")
    screen.SetMessagePort(port)
    scene = screen.CreateScene("ZipAppScene")
    screen.Show()
    scene.setFocus(true)

    while true
        msg = wait(0, port)
        if type(msg) = "roSGScreenEvent"
            if msg.IsScreenClosed() then return
        end if
    end while
end sub
