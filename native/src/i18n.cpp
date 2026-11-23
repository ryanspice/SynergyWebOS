#include "synergy/i18n.hpp"
#include <cstddef>
namespace synergy {
    namespace {
        struct Entry {
            std::string_view key, en, fr;
        };
        /* UI strings only. Protocol values, daemon errors and client log lines
           stay verbatim so log parsing and control contracts are unaffected. */
        constexpr Entry Catalog[] = {
            {"brand.sub","WEBOS / NATIVE","WEBOS / NATIF"},
            {"home.tagline","Your desk, on the TV.","Votre bureau, sur la TV."},
            {"tab.link","Connection","Connexion"},
            {"tab.input","Input","Entr\303\251e"},
            {"tab.handoff","Handoff","Relais"},
            {"tab.diagnostics","Diagnostics","Diagnostics"},
            {"close","Close UI","Fermer"},
            {"language","Language: EN","Langue : FR-CA"},
            {"state.active","active","actif"},
            {"state.connecting","connecting","connexion"},
            {"state.stopped","stopped","arrêté"},
            {"state.offline","daemon offline","démon hors ligne"},
            {"language.en","English","English"},
            {"language.fr","Fran\303\247ais (Qu\303\251bec)","Fran\303\247ais (Qu\303\251bec)"},
            {"language.changed","Language set to {name}. Saved with settings.","Langue r\303\251gl\303\251e sur {name}. Enregistr\303\251e avec les param\303\250tres."},

            {"notice.start_daemon","Start the separately provisioned daemon to connect.","D\303\251marrez le d\303\251mon provisionn\303\251 s\303\251par\303\251ment pour vous connecter."},
            {"notice.queue_full","Control queue is full; wait for the current request to finish.","La file de contr\303\264le est pleine ; attendez la fin de la requ\303\252te en cours."},
            {"notice.draft_changed","Draft changed. Save before starting a new connection.","Brouillon modifi\303\251. Enregistrez avant de d\303\251marrer une nouvelle connexion."},
            {"notice.schema","Daemon control schema {a} does not match UI schema {b}; update the app/daemon pair.","Le sch\303\251ma de contr\303\264le du d\303\251mon {a} ne correspond pas au sch\303\251ma de l’interface {b} ; mettez \303\240 jour la paire application/d\303\251mon."},
            {"notice.daemon_available","Daemon available. Dry run first; real input needs explicit approval.","D\303\251mon disponible. Essayez d’abord en simulation ; les vraies entr\303\251es n\303\251cessitent une approbation explicite."},
            {"notice.probe_complete","Probe complete. No virtual devices were created.","Sonde termin\303\251e. Aucun appareil virtuel n’a \303\251t\303\251 cr\303\251\303\251."},
            {"notice.config_saved","Configuration saved; a running connection keeps its active settings.","Configuration enregistr\303\251e ; une connexion active conserve ses param\303\250tres en cours."},
            {"notice.config_start","Configuration saved; start accepted. Check status for the connection result.","Configuration enregistr\303\251e ; d\303\251marrage accept\303\251. Consultez l’\303\251tat pour le r\303\251sultat de la connexion."},
            {"notice.profile_load","Local handoff reference loaded; desktop Synergy was not changed.","R\303\251f\303\251rence de relais locale charg\303\251e ; Synergy sur l’ordinateur n’a pas \303\251t\303\251 modifi\303\251."},
            {"notice.profile_save","Handoff reference saved locally. Apply server-side settings separately.","R\303\251f\303\251rence de relais enregistr\303\251e localement. Appliquez les param\303\250tres du serveur s\303\251par\303\251ment."},
            {"notice.request","Request accepted: {tag}. Check status for its result.","Requ\303\252te accept\303\251e : {tag}. Consultez l’\303\251tat pour son r\303\251sultat."},
            {"notice.dry_on","Dry run selected.","Simulation s\303\251lectionn\303\251e."},
            {"notice.real","Real input selected. Plaintext approval and a rooted TV are also required.","Vraies entr\303\251es s\303\251lectionn\303\251es. L’approbation du texte en clair et une TV root\303\251e sont aussi requises."},
            {"notice.plaintext","Unencrypted keyboard traffic is suitable only for an isolated trusted LAN.","Le trafic clavier non chiffr\303\251 ne convient qu’\303\240 un r\303\251seau local isol\303\251 et de confiance."},
            {"notice.cursor","Experimental one-pixel cursor nudge; not absolute pointer synchronization.","Micro-d\303\251placement exp\303\251rimental du curseur d’un pixel ; pas de synchronisation absolue du pointeur."},

            {"link.title","One desktop. One shared keyboard.","Un ordinateur. Un clavier partag\303\251."},
            {"link.lede","Connect to Synergy Core on your desktop; this is input sharing, not screen streaming.","Connectez-vous \303\240 Synergy Core sur votre ordinateur ; il s’agit de partage d’entr\303\251es, pas de diffusion d’\303\251cran."},
            {"field.server","DESKTOP LAN IP","IP LAN DE L’ORDINATEUR"},
            {"field.port","CORE PORT","PORT DU CORE"},
            {"field.name","TV SCREEN NAME","NOM DE L’\303\211CRAN TV"},
            {"field.width","PROTOCOL WIDTH","LARGEUR DU PROTOCOLE"},
            {"field.height","PROTOCOL HEIGHT","HAUTEUR DU PROTOCOLE"},
            {"field.hold","META HOLD / MILLISECONDS","MAINTIEN META / MILLISECONDES"},
            {"field.empty","Select to enter","S\303\251lectionner pour saisir"},
            {"dry.on","Dry run: ON","Simulation : ACTIV\303\211E"},
            {"dry.off","Real input selected","Vraies entr\303\251es s\303\251lectionn\303\251es"},
            {"save","Save settings","Enregistrer"},
            {"start","Save + Connect","Enregistrer et connecter"},
            {"stop","Stop / release input","Arr\303\252ter / lib\303\251rer"},
            {"probe","Probe TV","Sonder la TV"},
            {"guardrails.title","Connection guardrails","Garde-fous de connexion"},
            {"guardrail.1","01   Desktop runs Synergy server","01   L’ordinateur ex\303\251cute le serveur Synergy"},
            {"guardrail.2","02   Screen name matches server layout","02   Le nom d’\303\251cran correspond \303\240 la disposition du serveur"},
            {"guardrail.3","03   Prove dry-run events first","03   V\303\251rifiez d’abord les \303\251v\303\251nements en simulation"},
            {"guardrail.4","04   Root + uinput for real TV input","04   Root + uinput pour les vraies entr\303\251es TV"},
            {"events","Events: {n}","\303\211v\303\251nements : {n}"},
            {"saved_settings","Saved settings do not mutate an active session.","Les param\303\250tres enregistr\303\251s ne modifient pas une session active."},

            {"input.title","Deliberate control. Safe release.","Contr\303\264le d\303\251lib\303\251r\303\251. Lib\303\251ration s\303\273re."},
            {"input.lede","Meta opens Home; Alt stays normal until its five-second hold. Alt+Tab cycles running apps.","Meta ouvre l’accueil ; Alt reste normal jusqu’\303\240 un maintien de cinq secondes. Alt+Tab parcourt les applications actives."},
            {"meta_target","Meta target: {v}","Cible de Meta : {v}"},
            {"value.system","system","syst\303\250me"},
            {"value.off","off","d\303\251sactiv\303\251"},
            {"cursor.on","Cursor nudge: ON","Micro-d\303\251placement : ACTIV\303\211"},
            {"cursor.off","Cursor nudge: OFF","Micro-d\303\251placement : D\303\211SACTIV\303\211"},
            {"plaintext.approved","Plaintext input approved","Entr\303\251es en clair approuv\303\251es"},
            {"plaintext.approve","Approve plaintext input","Approuver les entr\303\251es en clair"},
            {"input.warning","Clipboard is not supported. The optional mTLS bridge carries controller input; never expose port 24800 to the Internet.","Le presse-papiers n'est pas pris en charge. Le pont mTLS optionnel transporte les entr\303\251es de la manette ; n'exposez jamais le port 24800 \303\240 Internet."},
            {"input.note","Absolute pointer positioning is implemented in the native client.","Le positionnement absolu du pointeur est impl\303\251ment\303\251 dans le client natif."},
            {"boot.disable","Disable boot connection","D\303\251sactiver la connexion au d\303\251marrage"},
            {"boot.enable","Enable boot connection","Activer la connexion au d\303\251marrage"},
            {"boot.note","Boot connection needs explicit root provisioning; legacy hooks are never replaced.","La connexion au d\303\251marrage n\303\251cessite un provisionnement root explicite ; les anciens hooks ne sont jamais remplac\303\251s."},

            {"handoff.title","Handoff is a server-side decision.","Le relais est d\303\251cid\303\251 c\303\264t\303\251 serveur."},
            {"handoff.lede","Keep a local reference for desktop settings. Saving here never edits your Windows Synergy configuration.","Conservez une r\303\251f\303\251rence locale des param\303\250tres de l’ordinateur. Enregistrer ici ne modifie jamais votre configuration Synergy Windows."},
            {"corner","Corner: {v}","Coin : {v}"},
            {"corner.bottom-right","bottom-right","bas-droite"},
            {"corner.bottom-left","bottom-left","bas-gauche"},
            {"corner.top-left","top-left","haut-gauche"},
            {"corner.top-right","top-right","haut-droite"},
            {"corner.reference","Corner reference: {n} px","R\303\251f\303\251rence de coin : {n} px"},
            {"delay.reference","Delay reference: {n} ms","R\303\251f\303\251rence de d\303\251lai : {n} ms"},
            {"handoff.warning","Corner preferences, without hidden desktop changes.","Pr\303\251f\303\251rences de coin, sans changement cach\303\251 sur l’ordinateur."},
            {"handoff.note","The legacy desktop helper uses a server edge approximation. This client does not implement a new server protocol.","L’ancien utilitaire de bureau utilise une approximation de bord du serveur. Ce client n’impl\303\251mente pas un nouveau protocole serveur."},
            {"profile.save","Save local reference","Enregistrer la r\303\251f\303\251rence locale"},
            {"profile.load","Load local reference","Charger la r\303\251f\303\251rence locale"},

            {"diag.title","Connection and input diagnostics.","Diagnostics de connexion et d’entr\303\251e."},
            {"diag.probe","Read-only input probe","Sonde d’entr\303\251e en lecture seule"},
            {"diag.ids","UI UID: {uid}   |   Controller UID: {ctrl}   |   C++20 / SDL2 / raw GLES2","UID de l’interface : {uid}   |   UID du contr\303\264leur : {ctrl}   |   C++20 / SDL2 / GLES2 brut"},
            {"diag.no_log","No daemon log available.","Aucun journal du d\303\251mon disponible."},
            {"diag.probe_label","PROBE","SONDE"},
            {"diag.root","Root: {v}","Root : {v}"},
            {"diag.uinput","uinput writable: {v}","uinput accessible en \303\251criture : {v}"},
            {"diag.yes","yes","oui"},
            {"diag.no","not established","non \303\251tabli"},
            {"diag.peer","Peer authentication: Unix UID","Authentification du pair : UID Unix"},
            {"diag.shared_uid","Shared app UID is a shared trust boundary.","Un UID d’application partag\303\251 est une fronti\303\250re de confiance partag\303\251e."},

            {"edit.server","Edit server","Modifier le serveur"},
            {"edit.port","Edit port","Modifier le port"},
            {"edit.name","Edit screen name","Modifier le nom de l’\303\251cran"},
            {"edit.width","Edit width","Modifier la largeur"},
            {"edit.height","Edit height","Modifier la hauteur"},
            {"edit.hold","Edit hold delay","Modifier le d\303\251lai de maintien"},
            {"edit.space","Space","Espace"},
            {"edit.delete","Delete","Supprimer"},
            {"edit.clear","Clear","Effacer"},
            {"edit.apply","Apply","Appliquer"},
            {"edit.cancel","Cancel","Annuler"},
            {"footer","D-pad / pointer to navigate    OK to select    Back to cancel    Close UI keeps the daemon running","D-pad / pointeur pour naviguer    OK pour s\303\251lectionner    Retour pour annuler    Fermer l’interface laisse le d\303\251mon actif"}
        };
    }
    Locale locale_from_string(std::string_view value){
        if(value=="en"||(value.size()>=3&&value.compare(0,3,"en-")==0))return Locale::En;
        if(value=="fr"||(value.size()>=3&&value.compare(0,3,"fr-")==0))return Locale::FrCA;
        return Locale::Unknown;
    }
    const char* locale_string(Locale locale){
        if(locale==Locale::FrCA)return "fr-CA";
        if(locale==Locale::En)return "en";
        return "";
    }
    const char* locale_name(Locale locale){
        if(locale==Locale::FrCA)return "Fran\303\247ais (Qu\303\251bec)";
        return "English";
    }
    std::string tr(std::string_view key,Locale locale,TrArgs args){
        for(const auto& entry:Catalog){
            if(entry.key!=key)continue;
            std::string out(locale==Locale::FrCA?entry.fr:entry.en);
            for(const auto& arg:args){
                const std::string token="{"+std::string(arg.first)+"}";
                size_t at=0;
                while((at=out.find(token,at))!=std::string::npos){
                    out.replace(at,token.size(),arg.second);
                    at+=arg.second.size();
                }
            }
            return out;
        }
        return std::string(key);
    }
    size_t catalog_size(){return sizeof(Catalog)/sizeof(Catalog[0]);}
    bool catalog_valid(){
        for(size_t i=0;i<catalog_size();++i){
            if(Catalog[i].key.empty()||Catalog[i].en.empty()||Catalog[i].fr.empty())return false;
            for(size_t j=0;j<i;++j)if(Catalog[i].key==Catalog[j].key)return false;
        }
        return true;
    }
}
