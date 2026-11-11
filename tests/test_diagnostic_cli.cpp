// Restricted parser only. Synthetic rejected arguments must not be echoed or change the caller's options.
#include "product/product_cli.h"
#include "product_test_util.h"
int main(){using namespace om;
    for(const auto &name:{"a.json","Report-01.json","report.v1.json","CON-ok.json","COM1-safe.json","lpt-name.json"})CHECK(diagnosticExportName(name));
    CHECK(diagnosticExportName(std::string(91,'a')+".json"));
    for(const auto &name:{"",".json",".private.json","a.JSON","a.json.","a..json","../a.json","a/b.json","a\\b.json","C:a.json","a b.json",
                         "a\n.json","con.json","CoN.extra.json","prn.json","aux.json","nul.json","COM0.json","com1.json","LPT9.json","lpt00.json","CONIN$.json","CLOCK$.json"})CHECK(!diagnosticExportName(name));
    CHECK(!diagnosticExportName(std::string(92,'a')+".json"));CHECK(!diagnosticExportName(std::string("a\0.json",7)));CHECK(!diagnosticExportName("\xc3\xa9.json"));
    const std::string digest(64,'a');CHECK(diagnosticDigest(digest));
    for(const auto &bad:{"","a","ABCDEF", "private-fixture-value"})CHECK(!diagnosticDigest(bad));
    CHECK(!diagnosticDigest(std::string(64,'g')));CHECK(!diagnosticDigest(std::string(64,'A')));CHECK(!diagnosticDigest(digest+"0"));
    const auto parse=[&](std::vector<std::string> args,bool good,bool selected=false,bool exported=false,bool help=false){
        args.insert(args.begin(),{"fixture","diagnostics"});std::vector<const char *> argv;for(const auto &arg:args)argv.push_back(arg.c_str());
        CliParse result;result.command=Command::Status;result.diagnostics.exportName="original-fixture.json";std::string error;
        CHECK(parseProductCli(static_cast<int>(argv.size()),argv.data(),&result,&error)==good);
        if(good){CHECK(result.command==Command::Diagnostics&&result.commandExplicit&&result.rest.empty()&&result.ov.configPath.empty());
            CHECK(result.diagnostics.selection.build==selected&&result.diagnostics.help==help&&result.help==help);
            CHECK(result.diagnostics.exportName==(exported?"report.json":"")&&result.diagnostics.confirmation==(exported?digest:""));}
        else {CHECK(result.command==Command::Status&&result.diagnostics.exportName=="original-fixture.json");
            CHECK(error.find("private-fixture-value")==error.npos&&error.find("invalid options")!=error.npos);}
    };
    parse({},true);parse({"--preview"},true);parse({"--include-build"},true,true);parse({"--preview","--include-build"},true,true);
    parse({"--export","report.json","--confirm",digest},true,false,true);
    parse({"--confirm",digest,"--include-build","--export","report.json"},true,true,true);
    parse({"--help"},true,false,false,true);parse({"-h"},true,false,false,true);
    for(const auto &args:std::vector<std::vector<std::string>>{
        {"--include-build","--include-build"},{"--preview","--preview"},{"--help","--include-build"},{"--preview","--export","report.json","--confirm",digest},
        {"--export"},{"--confirm"},{"--export","report.json"},{"--confirm",digest},{"--export","report.json","--confirm",digest,"--confirm",digest},
        {"--export","report.json","--export","report.json","--confirm",digest},{"--export","../private-fixture-value.json","--confirm",digest},
        {"--include-build=private-fixture-value"},{"--export=private-fixture-value"},{"--confirm=private-fixture-value"},
        {"--config","private-fixture-value"},{"--wallet","private-fixture-value"},{"--pool","private-fixture-value"},{"--password","private-fixture-value"},
        {"--devices","private-fixture-value"},{"--log-dir","private-fixture-value"},{"--language","private-fixture-value"},
        {"--json"},{"--bench"},{"--help-all"},{"--"},{"private-fixture-value"},{"--export","report.json","--confirm","private-fixture-value"}})parse(args,false);
    DiagnosticOptions original;original.exportName="original-fixture.json";std::string error;const char *bad[]={nullptr};
    CHECK(!parseDiagnosticCli(1,bad,0,&original,&error)&&original.exportName=="original-fixture.json");
    CHECK(!parseDiagnosticCli(0,bad,-1,&original,&error));CHECK(!parseDiagnosticCli(0,bad,1,&original,&error));
    CHECK(!parseDiagnosticCli(0,bad,0,nullptr,&error));
    return finishTests("restricted build diagnostic CLI and portable basename/digest checks");
}
