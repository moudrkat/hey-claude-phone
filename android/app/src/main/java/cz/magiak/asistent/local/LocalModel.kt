package cz.magiak.asistent.local

import android.content.Context
import org.json.JSONObject

/**
 * A 4M-parameter model that lives in the phone (assets/tiny/model.bin, 2 MB). It turns a sentence into a few
 * lines of a small language, and an interpreter runs them; the interpreter, not the model, decides what a
 * program may do. Nothing leaves the phone to think. C side: cpp/tinyhouse.c.
 */
object LocalModel {

    class Result(val program: String, val ok: Boolean, val error: String, val screen: Boolean,
                 val rows: List<Pair<Int, String>>, val said: String)

    @Volatile private var loaded = false

    @Synchronized
    private fun ensure(context: Context): Boolean {
        if (loaded) return true
        System.loadLibrary("tinyhouse")
        val bytes = context.assets.open("tiny/model.bin").use { it.readBytes() }
        loaded = nativeLoad(bytes)
        return loaded
    }

    /** Off the main thread: the model writes for a few hundred milliseconds. */
    @Synchronized
    fun ask(context: Context, sentence: String): Result {
        if (!ensure(context)) return Result("", false, "the local model did not load", false, emptyList(), "")
        // What reaches the model is what it was trained on: plain ASCII, as a speech recogniser would give it.
        val plain = sentence.filter { it.code in 32..126 }.trim()
        val program = nativeWrite(plain)
        if (program.isEmpty()) return Result("", false, "that sentence does not fit", false, emptyList(), "")
        notSaid(plain, program)?.let { return Result(program, false, "I did not catch that: $it was not said", false, emptyList(), "") }
        val json = JSONObject(nativeRun(program))
        if (!json.getBoolean("ok")) return Result(program, false, json.getString("error"), false, emptyList(), "")
        val rows = json.getJSONArray("rows").let { a -> (0 until a.length()).map { a.getJSONObject(it).run { getInt("size") to getString("text") } } }
        return Result(program, true, "", json.getBoolean("screen"), rows, json.getString("said"))
    }

    /**
     * A check outside the model: what a program asks a source for must have been said. The model should copy the
     * town, the coin or the day out of the sentence; when it writes weather("Paris") for a sentence about Prague,
     * no rule of the language is broken and the screen would be wrong with a straight face. One slip of the finger
     * is allowed ("bitcon" for "bitcoin"), another word is not. Returns the first call that was not said, or null.
     */
    fun notSaid(sentence: String, program: String): String? {
        val heard = WORD.findAll(sentence.lowercase().replace("'", "")).map { it.value }.toList()
        for (call in ASKED.findAll(program)) {
            val (fn, what) = call.destructured
            for (w in WORD.findAll(what.lowercase().replace("'", "")).map { it.value })
                if (w !in SMALL && heard.none { near(w, it) }) return "$fn(\"$what\")"
        }
        return null
    }

    private fun near(a: String, b: String): Boolean {
        if (a == b) return true
        if (minOf(a.length, b.length) < 4 || kotlin.math.abs(a.length - b.length) > 1) return false
        if (a.length == b.length) {
            val d = a.indices.filter { a[it] != b[it] }
            return d.size == 1 || (d.size == 2 && d[1] == d[0] + 1 && a[d[0]] == b[d[1]] && a[d[1]] == b[d[0]])
        }
        val (s, l) = if (a.length < b.length) a to b else b to a
        return l.indices.any { l.removeRange(it, it + 1) == s }
    }

    private val ASKED = Regex("""\b(weather|tomorrow|sunrise|sunset|air|price|until)\("([^"]*)"\)""")
    private val WORD = Regex("[a-z0-9]+")
    private val SMALL = setOf("the", "a", "an", "my", "of", "in")

    @JvmStatic private external fun nativeLoad(model: ByteArray): Boolean
    @JvmStatic private external fun nativeWrite(sentence: String): String
    @JvmStatic private external fun nativeRun(program: String): String
}
