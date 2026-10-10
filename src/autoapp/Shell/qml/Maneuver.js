.pragma library

// Arrow symbol for a maneuver name from nav.turn ("turn_normal_left", "roundabout_enter", ...).
function symbol(maneuver) {
    if (!maneuver)
        return "↑"
    if (maneuver.indexOf("u_turn") >= 0)
        return maneuver.indexOf("right") >= 0 ? "↷" : "↶"
    if (maneuver.indexOf("roundabout") >= 0)
        return "⟲"
    if (maneuver.indexOf("destination") >= 0)
        return "⚑"
    if (maneuver.indexOf("ferry") >= 0)
        return "⛴"
    if (maneuver.indexOf("sharp_left") >= 0)
        return "↙"
    if (maneuver.indexOf("sharp_right") >= 0)
        return "↘"
    if (maneuver.indexOf("slight_left") >= 0 || maneuver.indexOf("keep_left") >= 0 || maneuver.indexOf("fork_left") >= 0)
        return "↖"
    if (maneuver.indexOf("slight_right") >= 0 || maneuver.indexOf("keep_right") >= 0 || maneuver.indexOf("fork_right") >= 0)
        return "↗"
    if (maneuver.indexOf("left") >= 0)
        return "↰"
    if (maneuver.indexOf("right") >= 0)
        return "↱"
    return "↑"
}

// "350 m" / "1,2 km" from nav.distance or nav.destination
function distance(data) {
    if (!data)
        return ""
    if (data.display) {
        var unit = { meters: "m", kilometers: "km", kilometers_p1: "km", miles: "mi", miles_p1: "mi", feet: "ft", yards: "yd" }[data.unit] || ""
        return data.display.replace(".", ",") + (unit ? " " + unit : "")
    }
    if (data.meters === undefined)
        return ""
    return data.meters >= 1000 ? (data.meters / 1000).toFixed(1).replace(".", ",") + " km" : data.meters + " m"
}

function duration(seconds) {
    if (!seconds)
        return ""
    var minutes = Math.round(seconds / 60)
    return minutes >= 60 ? Math.floor(minutes / 60) + " h " + (minutes % 60) + " min" : minutes + " min"
}
