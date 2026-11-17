package cz.magiak.asistent.local

import android.content.Context

/**
 * Names never reach the model. A word it does not know is taken out of the sentence before the model sees it and
 * put back into the program afterwards:
 *
 *     "show the weather in Kladno"  ->  "show the weather in QA"  ->  weather("QA")  ->  weather("Kladno")
 *
 * A model this small does not learn to copy a word it has never seen (trained on 155 towns, it answered a 156th
 * with one of the 155). So copying is done here, by ordinary code, and cannot go wrong.
 *
 * A word is known if it is in assets/tiny/known_words.txt, or a number, or, written without a capital, one slip of
 * the finger away from a known word. Everything else is a name; neighbouring names are one name.
 * The same rules as slots.py in tiny-house, which the training data went through.
 */
object Slots {

    private val MARKS = listOf("QA", "QB", "QC", "QD", "QE", "QF")
    private const val EDGE = ",.!?;:"
    private var known: Set<String>? = null
    private var byLength: Map<Int, List<String>> = emptyMap()

    @Synchronized
    private fun load(context: Context): Set<String> {
        known?.let { return it }
        val words = context.assets.open("tiny/known_words.txt").bufferedReader().use { it.readText() }.split(Regex("\\s+")).filter { it.isNotEmpty() }.toSet()
        byLength = words.groupBy { it.length }
        known = words
        return words
    }

    private fun near(a: String, b: String): Boolean {
        if (a.length == b.length) {
            val d = a.indices.filter { a[it] != b[it] }
            return d.size == 1 || (d.size == 2 && d[1] == d[0] + 1 && a[d[0]] == b[d[1]] && a[d[1]] == b[d[0]])
        }
        val (s, l) = if (a.length < b.length) a to b else b to a
        return l.indices.any { l.removeRange(it, it + 1) == s }
    }

    private fun isKnown(words: Set<String>, word: String): Boolean {
        val w = word.lowercase().filter { it in 'a'..'z' || it in '0'..'9' }
        if (w.isEmpty() || w in words || w.any { it.isDigit() }) return true
        if (w.length < 4 || word.first().isUpperCase()) return false     // a capital says it is a name
        return (w.length - 1..w.length + 1).any { n -> byLength[n].orEmpty().any { near(w, it) } }
    }

    /** The sentence with names replaced by marks, and what each mark stands for. */
    fun hide(context: Context, sentence: String): Pair<String, Map<String, String>> {
        val words = load(context)
        val out = mutableListOf<String>()
        val names = linkedMapOf<String, String>()
        val run = mutableListOf<String>()
        fun close() {
            if (run.isEmpty()) return
            val text = run.joinToString(" ")
            val core = text.trim { it in EDGE }
            var mark = names.entries.firstOrNull { it.value.equals(core, ignoreCase = true) }?.key
            if (mark == null && names.size < MARKS.size) {
                mark = MARKS[names.size]
                names[mark] = core
            }
            out += (mark ?: core) + text.substring(text.trimEnd { it in EDGE }.length)
            run.clear()
        }
        for (tok in sentence.split(Regex("\\s+")).filter { it.isNotEmpty() }) {
            if (tok in MARKS || isKnown(words, tok)) {
                close()
                out += tok
            } else run += tok
        }
        close()
        return out.joinToString(" ") to names
    }

    fun restore(program: String, names: Map<String, String>): String =
        names.entries.fold(program) { text, (mark, name) -> text.replace(mark, name) }
}
