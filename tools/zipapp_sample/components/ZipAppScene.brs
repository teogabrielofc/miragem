' Miragem ZIP app sample - cena de prova.
sub init()
    m.top.setFocus(true)

    t = m.top.findNode("zipappTitle")
    if t <> invalid then t.text = "__ZIPAPP_MARKER__"

    s = m.top.findNode("zipappSub")
    if s <> invalid then s.text = "compiled from mounted ZIP - slot identity preserved"
end sub
