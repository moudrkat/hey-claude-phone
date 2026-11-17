package cz.magiak.asistent.local

import android.content.Context
import android.util.Log
import cz.magiak.asistent.Config
import cz.magiak.asistent.CustomScreen
import cz.magiak.asistent.R
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale

/**
 * The home screen from a sentence, without Claude: the local model writes a program, the interpreter turns it into
 * rows, and the rows become a page for CustomScreen. Live values are fetched by the page through the same
 * `asistent.json` the pages Claude writes use, under the same rules (once a minute, the last value on error).
 *
 * Far less than Claude can do: rows of text in three sizes, and the sources in res/raw/local_screen.html.
 * What it does not know it says so, and the screen stays as it was.
 */
object LocalScreen {

    /** Off the main thread. Returns what happened, for the log and for whoever speaks it. */
    fun show(context: Context, sentence: String): String {
        val started = System.currentTimeMillis()
        val r = LocalModel.ask(context, sentence)
        val took = System.currentTimeMillis() - started
        Log.i(Config.TAG, "Local: \"$sentence\" in $took ms\n${r.program}")
        if (!r.ok) return "refused: ${r.error}".also { Log.i(Config.TAG, "Local: $it") }
        if (!r.screen) return (if (r.said == "?") "not something I can do here" else "nothing for the screen").also { Log.i(Config.TAG, "Local: $it") }
        val rows = JSONArray()
        r.rows.forEach { (size, text) -> rows.put(JSONObject().put("size", size).put("text", text)) }
        val html = context.resources.openRawResource(R.raw.local_screen).bufferedReader().use { it.readText() }
            .replace("/*colors*/", CustomScreen.palette(context))
            .replace("{lang}", Locale.getDefault().toLanguageTag())
            .replace("{rows}", rows.toString().replace("</", "<\\/"))
        CustomScreen.save(context, html)
        return "done, ${r.rows.size} rows"
    }
}
