.pragma library

// Name and description of a plugin in the language of the shell: the plugin's own translation of
// "<id>.name" / "<id>.description" (i18n/<language>.qm), otherwise the text of its manifest (store entries of
// plugins that are not installed have no translation yet). A binding that calls this follows a language switch.
function text(plugin, key) {
    var id = plugin.id + "." + key
    var translated = qsTrId(id)
    return translated !== id ? translated : (plugin[key] || "")
}

function name(plugin) {
    return text(plugin, "name") || plugin.id
}

function description(plugin) {
    return text(plugin, "description")
}
