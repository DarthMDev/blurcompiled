// Independently recover missing baseline listings in one disposable project.
// @category BlurResearch
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
import java.util.*;

public class ExportTrapBatch extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3 || !args[2].matches("[0-9a-f]{64}"))
            throw new IllegalArgumentException("Expected TSV plan, fresh output directory, executable SHA-256");
        if (!args[2].equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Executable identity changed");
        Path out = Path.of(args[1]);
        Files.createDirectory(out);
        List<String> results = new ArrayList<>();
        for (String line : Files.readAllLines(Path.of(args[0]))) {
            monitor.checkCancelled();
            String[] row = line.split("\t");
            if (row.length != 2 || !row[0].matches("[0-9a-f]{8}") || !row[1].matches("[0-9a-f]{8}"))
                throw new IllegalArgumentException("Invalid plan row");
            try {
                runScript("ExportObservedFunction.java", new String[] {
                    out.resolve("observed-" + row[0]).toString(), args[2], row[0], row[1]
                });
                results.add(row[0] + "\texported");
            } catch (Exception error) {
                results.add(row[0] + "\treview\t" + error.toString().replace('\n', ' ').replace('\t', ' '));
            }
            Files.write(out.resolve("results.tsv"), results);
        }
    }
}
