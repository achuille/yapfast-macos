# macOS

Première version pour Apple Silicon, testée sur un MacBook Pro M1 Max sous macOS 27.0.1. Le paquet cible macOS 13 et suivants ; les versions plus anciennes que la machine de test et les Mac Intel n’ont pas été vérifiés.

## Installer

Prendre le ZIP dans les [releases](https://github.com/achuille/yapfast-macos/releases), le décompresser et copier Yapfast.app dans Applications. Aucun outil de développement n’est nécessaire pour l’utiliser.

La signature par défaut est ad hoc. Le paquet n’est pas notarisé ; une distribution sans avertissement Gatekeeper nécessite un certificat Developer ID et une notarisation Apple.

## Compiler

Installer les outils de ligne de commande Xcode (`xcode-select --install`), Rust via [rustup](https://rustup.rs), puis :

```sh
brew install cmake ninja pkgconf nasm
scripts/build-macos
open target/macos/Yapfast.app
```

Rust est fixé par `rust-toolchain.toml`. Le premier build télécharge les dépendances natives et peut prendre plusieurs dizaines de minutes. Prévoir 25 Go libres pour compiler et tester.

Le script utilise quatre tâches au maximum. Il range les fichiers intermédiaires dans `~/.local/share/yapfast-build/target`, hors des caches système et des dossiers synchronisés par iCloud ou Drive. `CARGO_TARGET_DIR` permet de choisir un autre emplacement. L’application et le ZIP sont produits dans `target/macos`.

## Intégration

- Barre de titre et presse-papiers macOS ; Cmd+V colle aussi les pièces jointes. Cmd+, ouvre les réglages depuis tout champ de la fenêtre, avec la virgule du clavier utilisé (AZERTY compris).
- Session et clé du cache dans le Trousseau, via le backend Apple de `keyring`.
- Metal par défaut pour les nouvelles installations. Le choix enregistré reste prioritaire. En cas d’échec du GPU, Yapfast redémarre en rendu logiciel.
- CoreAudio AudioQueue pour le micro et la sortie audio. Trois buffers de sortie réutilisables ; capture bornée à une seconde. Les threads attendent les callbacks audio.
- H.264 8 bits 4:2:0 décodé par VideoToolbox : fichiers, GIF vidéo et flux Annex B. Les images B des fichiers sont remises dans l’ordre de présentation ; les flux temps réel sont délivrés sans cette attente.
- AAC décodé par AudioToolbox, avec retrait des échantillons d’amorçage. VP8, VP9 et AV1 conservent les décodeurs du projet.
- Emoji couleur au format `sbix`, lisible par CoreText. Les polices système complètent les symboles, la ponctuation japonaise comme `・` et les alphabets décoratifs comme `𝘾𝙍𝐏`.
- Cache de ressources Metal limité à 32 Mio. Les textures encore nécessaires à l’affichage peuvent dépasser ce budget ; ce chiffre ne limite pas la mémoire totale de l’application.
- Les emoji en pause ou conservés après fermeture du sélecteur n’entretiennent plus une minuterie de rendu. Un rafraîchissement sans changement d’image ne force plus un nouveau dessin.
- Sur macOS 14 et suivants, la synchronisation d’affichage se met en pause lorsque le système masque la fenêtre. Le dessin en attente reprend quand elle redevient visible.
- Catégories Objective-C de WebRTC liées dans les exécutables. Leur absence faisait planter l’ouverture des réglages audio.

Les API de format utilisées sont documentées par [Apple pour sbix](https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6sbix.html). Le traitement des descripteurs AAC suit le format MPEG-4 également utilisé par [le décodeur AudioToolbox de FFmpeg](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/audiotoolboxdec.c).

## Vérifications

```sh
scripts/check --fast
scripts/test-fake-discord    # nécessite Bun
scripts/check-macos-native
```

Le premier script vérifie le formatage, les liens, Clippy, les tests du workspace, la compilation et le démarrage de contrôle. Le second couvre les échanges avec un faux serveur Discord local.

Les tests natifs s’exécutent avec AddressSanitizer et UndefinedBehaviorSanitizer. Ils vérifient la sortie CoreAudio en PCM16 et float32, la saturation de la file, la purge et la reprise, ainsi qu’un périphérique invalide. Ils décodent une image H.264 et vérifient le rejet d’en-têtes tronqués. Le test audio envoie du silence ; il n’enregistre rien.

Les tests Rust couvrent aussi la lecture H.264/AAC, la chronologie audio/vidéo, le retour en arrière, la lecture pendant le téléchargement, les emoji et l’envoi de messages dans l’aperçu local.

Sur le service Discord réel : authentification, liste des serveurs et lecture des conversations vérifiées. Le compte de test initial a été retiré du Trousseau ; la session utilisateur existante est conservée lors des mises à jour. Aucun message externe envoyé pendant ces vérifications.

## Limites

- Caméra, capture du son des applications, raccourcis vocaux globaux, icône dans la barre de menus, notifications et lancement à la connexion non portés.
- Appels réels, qualité du micro et partage d’écran à valider. Les tests de protocole et l’ouverture des périphériques ne suffisent pas à les confirmer.
- H.264 10 bits, 4:2:2 et 4:4:4 non pris en charge par ce chemin VideoToolbox. Les tests vérifient leur refus ; les formats profonds AV1/VP9 restent testés.
- Signature ad hoc : changer de build peut demander de nouveau l’accès au Trousseau. Choisir « Toujours autoriser » conserve l’autorisation pour les relancements du même build. Un certificat Apple Development ou Developer ID stable est nécessaire pour conserver l’identité du Trousseau entre builds ; un certificat autosigné ne suffit pas sur le Mac testé.
- Pas de promesse de mémoire ou de vitesse en appel : les mesures locales sur l’aperçu ne couvrent pas cet usage.

Ne pas placer de jeton dans le dépôt, un argument de commande ou une capture. Le probe de développement accepte `store-token --stdin` pour lire un secret via un pipe.

Pour mesurer une session déjà ouverte : `python3 scripts/measure-macos.py PID --seconds 20`. Le script relève le CPU (un cœur = 100 %), l’empreinte physique et la mémoire graphique. Comparer la même conversation, la même taille de fenêtre et le même parcours de défilement, après chargement des images ; ne pas compiler pendant la mesure. Les chiffres Linux du projet d’origine utilisent une autre définition de la mémoire et du CPU.

## Validation du 7 novembre 2026

Le paquet Apple Silicon a passé `scripts/check --release` : 905 tests Rust, Clippy sans avertissement, compilation et contrôle de démarrage. Les variantes Matrix ont passé 909 tests (en grande partie communs à la suite Discord), Clippy et les contrôles de compilation ; la version sans interface passe aussi son contrôle de démarrage. Le contrôle Clippy Matrix a été relancé après correction des conditions de compilation de deux fonctions de test Linux. Les tests contre le faux serveur ont passé 64 scénarios supplémentaires avec `YAPFAST_TEST_PROFILE=release`. Les tests natifs avec sanitizers passent également.

Le paquet installé dans Applications a été ouvert depuis le Finder : écran de connexion, aperçu local, Settings et Voice & video vérifiés. Sur la session utilisateur, Cmd+, ouvre les réglages avec un clavier AZERTY ; `・é𝘾𝐏中あ` s’affiche dans le champ de filtrage. Le défilement et la reprise après réduction de la fenêtre ont aussi été vérifiés. Les bibliothèques chargées viennent toutes de macOS ; aucun chemin Homebrew n’est requis à l’exécution. L’archive a été vérifiée après compression. Un relancement du paquet installé a restauré la session sans nouveau dialogue du Trousseau.

Pour signer avec une identité Apple déjà présente dans le Trousseau :

```sh
YAPFAST_SIGNING_IDENTITY="Developer ID Application: Nom (TEAMID)" scripts/build-macos
```

Un nom ou une empreinte de certificat convient. Une identité introuvable fait échouer la signature ; le script ne revient pas silencieusement à une signature ad hoc. La clé privée ne doit jamais être ajoutée au dépôt. Voir la [documentation Apple sur les identités et exigences de signature](https://developer.apple.com/documentation/technotes/tn3127-inside-code-signing-requirements).
